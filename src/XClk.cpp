#pragma once

#include "XClk.h"
#include "driver/rmt.h"
#include "driver/periph_ctrl.h"

bool ClockEnable(int pin, int Hz)
{
    // Use RMT peripheral to generate a stable square-wave XCLK. RMT is
    // precise and avoids LEDC/esp_clk_tree issues on some cores.
    rmt_config_t rmt_tx;
    rmt_tx.rmt_mode = RMT_MODE_TX;
    rmt_tx.channel = RMT_CHANNEL_0;
    rmt_tx.gpio_num = (gpio_num_t)pin;
    rmt_tx.mem_block_num = 1;
    // Use high-resolution clock (clk_div = 1 -> 80MHz base)
    rmt_tx.clk_div = 1;
    rmt_tx.tx_config.loop_en = true;
    rmt_tx.tx_config.carrier_en = false;
    rmt_tx.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    rmt_tx.tx_config.carrier_level = RMT_CARRIER_LEVEL_HIGH;

    if (rmt_config(&rmt_tx) != ESP_OK) return false;
    if (rmt_driver_install(rmt_tx.channel, 0, 0) != ESP_OK) return false;

    const uint32_t APB_CLK = 80000000UL;
    // ticks for half period: APB_CLK / (clk_div * freq * 2)
    uint32_t ticks = (APB_CLK) / (rmt_tx.clk_div * (uint32_t)Hz * 2U);
    if (ticks < 1) ticks = 1;
    if (ticks > 0x7fff) ticks = 0x7fff; // limit for safety

    rmt_item32_t items[2];
    items[0].level0 = 1; items[0].duration0 = ticks; items[0].level1 = 0; items[0].duration1 = ticks;
    items[1] = items[0];

    // Write items in loop mode (loop_en set above)
    if (rmt_write_items(rmt_tx.channel, items, 2, true) != ESP_OK) return false;

    return true;
}

void ClockDisable()
{
    periph_module_disable(PERIPH_LEDC_MODULE);
}
