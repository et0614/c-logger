#include "usb_comm.h"
#include "json_protocol.h"

#include "mcc_generated_files/usb/usb_device.h"                        // USBDevice_Handle
#include "mcc_generated_files/usb/usb_cdc/usb_cdc_virtual_serial_port.h" // USB_CDCxxx
#include "mcc_generated_files/timer/delay.h"                          // DELAY_milliseconds

void USB_Comm_Initialize(void)
{
    // USB デバイス層 (USBDevice_Initialize) は SYSTEM_Initialize() 済み。
    // ここでは CDC 仮想シリアルポート層のみ初期化する。
    USB_CDCVirtualSerialPortInitialize();
}

void USB_Comm_Task(void)
{
    // 1. USB スタックを駆動 (エニュメレーション応答・TX/RX の進行)
    USBDevice_Handle();
    USB_CDCVirtualSerialPortHandler();

    // 2. 受信バッファをドレインして 1 文字ずつコマンドパーサに渡す。
    //    while で空になるまで読み切ることで 1 tick の受信スループットを稼ぐ。
    uint8_t ch;
    while (USB_CDCRead(&ch) == CDC_SUCCESS)
    {
        JP_AppendChar((char)ch);
    }
}

uint16_t USB_Comm_SendString(const char *str)
{
    uint16_t count = 0;

    // バッファフル時の総待機時間上限 [ms]。大きめのレスポンスを 64B パケット
    // ずつ送る間にホストが読み出すのを待つため、ある程度長く取る。
    int budget_ms = 100;

    while (*str)
    {
        if (USB_CDCWrite((uint8_t)*str) == CDC_SUCCESS)
        {
            str++;
            count++;
        }
        else
        {
            // 送信バッファフル: USB スタックを駆動してドレインを進める
            USB_CDCVirtualSerialPortHandler();
            if (budget_ms-- <= 0) break;
            DELAY_milliseconds(1);
        }
    }
    return count;
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

uint16_t USB_Comm_SendBytes(const uint8_t *data, uint16_t len)
{
    // 生バイナリ送信 (0x00 を含むデータOK)。dump のバルク転送用。
    uint16_t count = 0;
    int budget_ms = 1000; // バルク用に長め (ホストが読まない時の待ち上限)

    while (count < len)
    {
        if (USB_CDCWrite(data[count]) == CDC_SUCCESS)
        {
            count++;
        }
        else
        {
            // 送信バッファフル: USB スタックを駆動してドレインを進める
            USB_CDCVirtualSerialPortHandler();
            if (budget_ms-- <= 0) break;
            DELAY_milliseconds(1);
        }
    }
    return count;
}
