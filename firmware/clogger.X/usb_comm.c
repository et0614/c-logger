#include "usb_comm.h"
#include "json_protocol.h"

#include "mcc_generated_files/usb/usb_device.h"                        // USBDevice_Handle
#include "mcc_generated_files/usb/usb_cdc/usb_cdc_virtual_serial_port.h" // USB_CDCxxx
#include "mcc_generated_files/usb/usb_cdc/usb_cdc.h"                   // USB_CDCRequestHandler
#include "mcc_generated_files/usb/usb_common/usb_core_events.h"       // USB_ClassRequestCallbackRegister
#include "mcc_generated_files/usb/usb_common/usb_core_transfer.h"     // USB_TransferControlDataSet
#include "mcc_generated_files/timer/delay.h"                          // DELAY_milliseconds
#include <avr/io.h>                                                   // USB0 / USB_ENABLE_bm / USB_ATTACH_bm
#include <avr/wdt.h>                                                  // wdt_reset
#include <stdbool.h>

// 【エラッタ対策】SET_LINE_CODING/GET_LINE_CODING を偶数アドレスのバッファで処理する。
// MCC の usb_cdc.c は通信設定(usbCDCLineCoding, 7バイト)のアライメントを指定していないため、
// リンク結果によって奇数アドレスに置かれることがある。マルチパケット有効のOUT転送は偶数アドレス
// 必須 (AVR64DU エラッタ 2.8.1) で、スタックは奇数アドレスを ENDPOINT_ALIGN_ERROR で拒否する。
// その結果 SET_LINE_CODING のデータ段階が完了せず、PC側でポートを開く処理がタイムアウトする。
// (K-Logger で発生。c-logger は現状たまたま偶数アドレスだが、コード変更で再発し得るため同じ対策を入れる。
//  生成コードを直すとMCC再生成で消えるのでここで対処)
static USB_CDC_LINE_CODING_t s_lineCoding __attribute__((aligned(2))) = {
    .dwDTERate   = 115200,
    .bCharFormat = USB_CDC_LINE_CODING_ONE_STOP_BIT,
    .bParityType = USB_CDC_LINE_CODING_PARITY_NONE,
    .bDataBits   = USB_CDC_LINE_CODING_8_DATA_BITS,
};

static RETURN_CODE_t lineCodingRequestHandler(USB_SETUP_REQUEST_t *req)
{
    if (USB_REQUEST_RECIPIENT_INTERFACE == (USB_REQUEST_RECIPIENT_t)req->bmRequestType.recipient &&
        USB_REQUEST_TYPE_CLASS == (USB_REQUEST_TYPE_t)req->bmRequestType.type &&
        (USB_CDC_REQUEST_SET_LINE_CODING == req->bRequest || USB_CDC_REQUEST_GET_LINE_CODING == req->bRequest))
    {
        return USB_TransferControlDataSet((uint8_t *)&s_lineCoding, sizeof(s_lineCoding), NULL);
    }
    return USB_CDCRequestHandler(req); // その他(SET_CONTROL_LINE_STATE等)は従来どおり
}

// SOF (1ms ごとのフレーム開始) を受けたか。ホストとつながって動いている間は立ち続ける。
static volatile bool s_sof_seen = false;
static void sofHandler(void) { s_sof_seen = true; }

// 直前の送信がホストに読まれず時間切れになった。バッファが空くまで以後の送信はすぐ諦める
// (ホストが読んでいないのに応答ごとに数秒待つと、dump などで長時間ふさがるため)。
static bool s_tx_stalled = false;

void USB_Comm_Initialize(void)
{
    // USB デバイス層 (USBDevice_Initialize) は呼び出し済みであること。
    // ここでは CDC 仮想シリアルポート層のみ初期化する。
    USB_CDCVirtualSerialPortInitialize();
    USB_ClassRequestCallbackRegister(lineCodingRequestHandler); // CDCの要求処理を上書き登録
    USB_SOFCallbackRegister(sofHandler);
    s_sof_seen = false;
    s_tx_stalled = false;
}

bool USB_Comm_SofSeen(void)
{
    bool seen = s_sof_seen;
    s_sof_seen = false;
    return seen;
}

int8_t USB_Comm_Fault(void)
{
    // MCC の USB スタックは一度エラーを返すと以後の処理を止める (usb_device.c の usbStatus)。
    // 未対応の要求や EP0 の状態ずれでも起きるので、呼び出し側で検出して USB を作り直す。
    RETURN_CODE_t st = USBDevice_StatusGet();
    return (st == SUCCESS) ? 0 : (int8_t)st;
}

void USB_Comm_Task(void)
{
    // 1. USB スタックを駆動 (エニュメレーション応答・TX/RX の進行)
    USB_Comm_Pump();

    // 2. 受信バッファをドレインして 1 文字ずつコマンドパーサに渡す。
    //    while で空になるまで読み切ることで 1 tick の受信スループットを稼ぐ。
    uint8_t ch;
    while (USB_CDCRead(&ch) == CDC_SUCCESS)
    {
        JP_AppendChar((char)ch);
    }
}

// 送信バッファが空(=直前の転送が完了し head/tail がリセット済み)になるまで
// USB スタックを駆動して待つ。
//
// 【なぜ必要か】CDC 送信バッファ(usb_cdc_virtual_serial_port.c)は循環バッファを
// 「リニア利用」している: USB_CDCWrite が head を進め、Handler が head バイトを
// 一括送出し、完了コールバックで head=tail=0 にリセットする。このため【転送中
// (in-flight)に USB_CDCWrite で追記すると、完了時の head リセットで追記分が丸ごと
// 破棄される(=バイト欠落)】。これを避けるため、追記の前に必ずバッファが空
// (CIRCBUF_Empty: head==tail)になるまで待ち、「転送中は追記しない」を保証する。
// @return true:空になった / false:budget_ms 内に空にならなかった(ホスト未読など)
static bool wait_tx_drained(int budget_ms)
{
    while (!CIRCBUF_Empty(&usbCDCTransmitBuffer))
    {
        USB_Comm_Pump();
        if (USB_Comm_Fault() != 0) return false; // スタックが止まった: 待っても進まない
        if (budget_ms-- <= 0) return false;
        DELAY_milliseconds(1);
    }
    return true;
}

// データを「バッファが空になるまで待つ→満杯まで詰める→転送開始」の繰り返しで送る。
// 転送中の追記を一切行わないのでバイト欠落しない。TXバッファ(512B)単位で送出するため
// 大きなデータでも往復回数は少ない (例: 4.4KB ≒ 9 往復)。
// @return 実際に送信できたバイト数
static uint16_t send_block(const uint8_t *data, uint16_t len)
{
    uint16_t sent = 0;

    // 前回ホストに読まれなかった分がまだ残っていれば、待たずに諦める
    if (s_tx_stalled)
    {
        USB_Comm_Pump();
        if (!CIRCBUF_Empty(&usbCDCTransmitBuffer)) return 0;
        s_tx_stalled = false;
    }

    while (sent < len)
    {
        // 直前の転送完了を待つ(ここで追記しない=欠落防止の肝)
        if (!wait_tx_drained(2000)) { s_tx_stalled = true; return sent; }

        // 空いたバッファに入るだけ詰める (満杯=BUFFER_FULL で停止)
        while (sent < len && USB_CDCWrite(data[sent]) == CDC_SUCCESS)
            sent++;

        // この塊の転送を開始 (Handler が head バイトを WriteStart)
        USB_Comm_Pump();
    }
    // 最後の塊を確実にホストへ送出 (次の送信が転送中追記で潰さないよう空を待つ)
    if (!wait_tx_drained(2000)) s_tx_stalled = true;
    return sent;
}

uint16_t USB_Comm_SendString(const char *str)
{
    uint16_t n = 0;
    while (str[n]) n++;
    return send_block((const uint8_t *)str, n);
}

void USB_Comm_Flush(void)
{
    // 送信バッファの中身を確実にホストへ押し出す (STANDBYスリープ移行直前など、
    // 応答を出してから寝るケースで使う)。USBスタックを少し多めに駆動して TX を drain する。
    for (int i = 0; i < 50; i++)
    {
        USB_CDCVirtualSerialPortHandler();
        DELAY_milliseconds(1);
    }
}

void USB_Comm_Pump(void)
{
    // USBスタック全体を1回駆動する。長時間のバルク送信(dump)中も、デバイスコア
    // (バスイベント/EP0応答)とCDC送出を進めることで、「デバイス無応答化によるホスト側
    // パイプ停止(転送が途中で止まる)」を防ぐ。
    // ウォッチドッグ (USB通信モードでのみ有効) もここで払う。スタックの中で固まれば払われずリセットされる。
    wdt_reset();
    USBDevice_Handle();
    USB_CDCVirtualSerialPortHandler();
}

uint16_t USB_Comm_SendBytes(const uint8_t *data, uint16_t len)
{
    // 生バイナリ送信 (0x00 を含むデータOK)。dump のバルク転送用。
    // 文字列送信と同じ欠落しない送出経路 (send_block) を使う。
    return send_block(data, len);
}

void USB_Comm_Disable(void)
{
    // ロギング(省電力)移行時にUSBモジュールを完全停止する。
    // 【なぜ】USBが有効(USBEN=1)だと主発振 OSCHF(12MHz) をSTANDBY中も要求し続け、MCUが
    // 実質スリープできず ~2mA 消費する(実測)。detach + USBEN=0 で OSCHF が STANDBY で
    // 停止できるようにし、ベース電流を大幅に下げる。
    // 【前提】最後の応答は呼び出し側が USB_Comm_Flush() 済みであること(これ以降USB送信不可)。
    // 【復帰】USBDevice_Initialize() → USB_Comm_Initialize() で作り直すか、リセットする。
    USB0.CTRLB &= ~USB_ATTACH_bm;   // バスから detach (ホストには切断として見える)
    USB0.CTRLA &= ~USB_ENABLE_bm;   // USBモジュール無効化 → OSCHFクロック要求を解除
    // アドレスを消す。残したまま attach し直すと、アドレス未設定のはずの間も「エニュメレーション済み」に
    // 見え、接続直後の Suspend (ホストのデバウンス中の無通信) を「抜かれた」と誤って扱ってしまう。
    USB0.ADDR = 0;
    // 送りかけのデータを捨てる (作り直した後に古い応答が出ていかないように)
    usbCDCTransmitBuffer.head = 0;
    usbCDCTransmitBuffer.tail = 0;
    s_tx_stalled = false;
}
