#include "mcc_generated_files/nvm/nvm.h" // EEPROM_Read / EEPROM_Write
#include "eeprom_manager.h"
#include "crc.h"
#include <stddef.h>
#include <util/atomic.h>

// AVR64DU32 の EEPROM 開始アドレス
#define EEPROM_BASE_ADDR  0x1400

// EEPROM の並びの版。並びを変えたら上げる (起動時に既定値で初期化し直す)
#define EM_MAGIC           'C'
#define EM_LAYOUT_VERSION  1

typedef struct {
    uint8_t magic;              // EM_MAGIC
    uint8_t layout;             // EM_LAYOUT_VERSION
    MeasurementSettings settings;
    Calibration calib;
} EepromMap;

#define ADDR_MAGIC     (EEPROM_BASE_ADDR + offsetof(EepromMap, magic))
#define ADDR_LAYOUT    (EEPROM_BASE_ADDR + offsetof(EepromMap, layout))
#define ADDR_SETTINGS  (EEPROM_BASE_ADDR + offsetof(EepromMap, settings))
#define ADDR_CALIB     (EEPROM_BASE_ADDR + offsetof(EepromMap, calib))

MeasurementSettings EM_settings;
Calibration EM_calib;

// <editor-fold defaultstate="collapsed" desc="EEPROM の読み書き">

static void write_byte(uint16_t addr, uint8_t v)
{
    while (EEPROM_IsBusy());
    if (EEPROM_Read(addr) == v) return; // 同じ値なら書かない (書き換え回数を減らす)
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        EEPROM_Write(addr, v);
    }
}

static void write_block(const void *src, uint16_t addr, size_t size)
{
    const uint8_t *p = (const uint8_t *)src;
    for (size_t i = 0; i < size; i++) write_byte(addr + i, p[i]);
    while (EEPROM_IsBusy()); // 書き込み完了を待つ
}

static void read_block(void *dst, uint16_t addr, size_t size)
{
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < size; i++) p[i] = EEPROM_Read(addr + i);
}

// 末尾の crc メンバーを除いた CRC
static uint8_t crc_of(const void *p, size_t size_with_crc)
{
    return CRC_calc8((uint8_t *)p, (uint8_t)(size_with_crc - 1));
}

// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="既定値">

static void defaultSettings(void)
{
    EM_settings = (MeasurementSettings){
        .interval = 60,
        .start_dt = 0,
    };
}

static void defaultCalibration(void)
{
    const LinearCorr none = { EM_CORR_A_SCALE, 0 }; // a = 1, b = 0 (補正なし)
    EM_calib = (Calibration){
        .temp = none,
        .hum = none,
        .co2 = none,
        .clock_ppm = 0,
    };
}

// </editor-fold>

void EM_saveSettings(void)
{
    EM_settings.crc = crc_of(&EM_settings, sizeof(EM_settings));
    write_block(&EM_settings, ADDR_SETTINGS, sizeof(EM_settings));
}

void EM_saveCalibration(void)
{
    EM_calib.crc = crc_of(&EM_calib, sizeof(EM_calib));
    write_block(&EM_calib, ADDR_CALIB, sizeof(EM_calib));
}

void EM_loadEEPROM(void)
{
    // 未初期化 (消去状態) か並びが変わったら、全体を既定値にする
    if (EEPROM_Read(ADDR_MAGIC) != EM_MAGIC || EEPROM_Read(ADDR_LAYOUT) != EM_LAYOUT_VERSION)
    {
        defaultSettings();
        defaultCalibration();
        EM_saveSettings();
        EM_saveCalibration();
        write_byte(ADDR_LAYOUT, EM_LAYOUT_VERSION);
        write_byte(ADDR_MAGIC, EM_MAGIC);   // 最後に書く (途中で電源が落ちたら次回やり直す)
        while (EEPROM_IsBusy());
        return;
    }

    // ブロックごとに CRC を確かめ、壊れていれば既定値に戻す
    read_block(&EM_settings, ADDR_SETTINGS, sizeof(EM_settings));
    if (EM_settings.crc != crc_of(&EM_settings, sizeof(EM_settings)))
    {
        defaultSettings();
        EM_saveSettings();
    }
    read_block(&EM_calib, ADDR_CALIB, sizeof(EM_calib));
    if (EM_calib.crc != crc_of(&EM_calib, sizeof(EM_calib)))
    {
        defaultCalibration();
        EM_saveCalibration();
    }
}
