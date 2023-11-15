/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 "Eric Poulsen" <eric@zyxod.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>

#include <time.h>
#include <sys/time.h>
#include "soc/rtc_cntl_reg.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_heap_caps.h"
#include "multi_heap.h"
#include "esp_pm.h"

#include "py/nlr.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "shared/timeutils/timeutils.h"
#include "modmachine.h"
#include "machine_rtc.h"
#include "modesp32.h"

// These private includes are needed for idf_heap_info.
#define MULTI_HEAP_FREERTOS
#include "../multi_heap_platform.h"
#include "../heap_private.h"

STATIC mp_obj_t esp32_wake_on_touch(const mp_obj_t wake) {

    if (machine_rtc_config.ext0_pin != -1) {
        mp_raise_ValueError(MP_ERROR_TEXT("no resources"));
    }
    // mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("touchpad wakeup not available for this version of ESP-IDF"));

    machine_rtc_config.wake_on_touch = mp_obj_is_true(wake);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp32_wake_on_touch_obj, esp32_wake_on_touch);

STATIC mp_obj_t esp32_wake_on_ext0(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    if (machine_rtc_config.wake_on_touch) {
        mp_raise_ValueError(MP_ERROR_TEXT("no resources"));
    }
    enum {ARG_pin, ARG_level};
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_pin,  MP_ARG_OBJ, {.u_obj = mp_obj_new_int(machine_rtc_config.ext0_pin)} },
        { MP_QSTR_level,  MP_ARG_BOOL, {.u_bool = machine_rtc_config.ext0_level} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_pin].u_obj == mp_const_none) {
        machine_rtc_config.ext0_pin = -1; // "None"
    } else {
        gpio_num_t pin_id = machine_pin_get_id(args[ARG_pin].u_obj);
        if (pin_id != machine_rtc_config.ext0_pin) {
            if (!RTC_IS_VALID_EXT_PIN(pin_id)) {
                mp_raise_ValueError(MP_ERROR_TEXT("invalid pin"));
            }
            machine_rtc_config.ext0_pin = pin_id;
        }
    }

    machine_rtc_config.ext0_level = args[ARG_level].u_bool;
    machine_rtc_config.ext0_wake_types = MACHINE_WAKE_SLEEP | MACHINE_WAKE_DEEPSLEEP;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(esp32_wake_on_ext0_obj, 0, esp32_wake_on_ext0);

STATIC mp_obj_t esp32_wake_on_ext1(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum {ARG_pins, ARG_level};
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_pins, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_level, MP_ARG_BOOL, {.u_bool = machine_rtc_config.ext1_level} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    uint64_t ext1_pins = machine_rtc_config.ext1_pins;


    // Check that all pins are allowed
    if (args[ARG_pins].u_obj != mp_const_none) {
        size_t len = 0;
        mp_obj_t *elem;
        mp_obj_get_array(args[ARG_pins].u_obj, &len, &elem);
        ext1_pins = 0;

        for (int i = 0; i < len; i++) {

            gpio_num_t pin_id = machine_pin_get_id(elem[i]);
            if (!RTC_IS_VALID_EXT_PIN(pin_id)) {
                mp_raise_ValueError(MP_ERROR_TEXT("invalid pin"));
                break;
            }
            ext1_pins |= (1ll << pin_id);
        }
    }

    machine_rtc_config.ext1_level = args[ARG_level].u_bool;
    machine_rtc_config.ext1_pins = ext1_pins;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(esp32_wake_on_ext1_obj, 0, esp32_wake_on_ext1);

STATIC mp_obj_t esp32_wake_on_ulp(const mp_obj_t wake) {
    if (machine_rtc_config.ext0_pin != -1) {
        mp_raise_ValueError(MP_ERROR_TEXT("no resources"));
    }
    machine_rtc_config.wake_on_ulp = mp_obj_is_true(wake);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp32_wake_on_ulp_obj, esp32_wake_on_ulp);

STATIC mp_obj_t esp32_gpio_deep_sleep_hold(const mp_obj_t enable) {
    if (mp_obj_is_true(enable)) {
        gpio_deep_sleep_hold_en();
    } else {
        gpio_deep_sleep_hold_dis();
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp32_gpio_deep_sleep_hold_obj, esp32_gpio_deep_sleep_hold);

#if CONFIG_IDF_TARGET_ESP32

#include "soc/sens_reg.h"

STATIC mp_obj_t esp32_raw_temperature(void) {
    SET_PERI_REG_BITS(SENS_SAR_MEAS_WAIT2_REG, SENS_FORCE_XPD_SAR, 3, SENS_FORCE_XPD_SAR_S);
    SET_PERI_REG_BITS(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_CLK_DIV, 10, SENS_TSENS_CLK_DIV_S);
    CLEAR_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP);
    CLEAR_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_DUMP_OUT);
    SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP_FORCE);
    SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP);
    esp_rom_delay_us(100);
    SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_DUMP_OUT);
    esp_rom_delay_us(5);
    int res = GET_PERI_REG_BITS2(SENS_SAR_SLAVE_ADDR3_REG, SENS_TSENS_OUT, SENS_TSENS_OUT_S);

    return mp_obj_new_int(res);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(esp32_raw_temperature_obj, esp32_raw_temperature);

#endif

STATIC mp_obj_t esp32_idf_heap_info(const mp_obj_t cap_in) {
    mp_int_t cap = mp_obj_get_int(cap_in);
    multi_heap_info_t info;
    heap_t *heap;
    mp_obj_t heap_list = mp_obj_new_list(0, 0);
    SLIST_FOREACH(heap, &registered_heaps, next) {
        if (heap_caps_match(heap, cap)) {
            multi_heap_get_info(heap->heap, &info);
            mp_obj_t data[] = {
                MP_OBJ_NEW_SMALL_INT(heap->end - heap->start), // total heap size
                MP_OBJ_NEW_SMALL_INT(info.total_free_bytes),   // total free bytes
                MP_OBJ_NEW_SMALL_INT(info.largest_free_block), // largest free contiguous
                MP_OBJ_NEW_SMALL_INT(info.minimum_free_bytes), // minimum free seen
            };
            mp_obj_t this_heap = mp_obj_new_tuple(4, data);
            mp_obj_list_append(heap_list, this_heap);
        }
    }
    return heap_list;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp32_idf_heap_info_obj, esp32_idf_heap_info);

#if CONFIG_PM_ENABLE

STATIC mp_obj_t esp32_pm_dump_locks(void) {
    esp_err_t err = esp_pm_dump_locks(stdout);
    if (err != ESP_OK) {
         mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Error 0x%04x"), err);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(esp32_pm_dump_locks_obj, esp32_pm_dump_locks);

typedef struct _esp_pm_lock_obj_t {
    mp_obj_base_t base;
    mp_obj_t name;
    esp_pm_lock_handle_t lock;
} esp32_pm_lock_obj_t;

STATIC void esp32_pm_lock_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    esp32_pm_lock_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "Pmlock(%8x", self->lock);
    if (self->name != mp_const_none) {
        mp_printf(print, ", \"%s\"", mp_obj_str_get_str(self->name));
    }
    mp_print_str(print, ")");
}

STATIC mp_obj_t esp32_pm_lock_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 2, false);

    const int lock_type = MP_OBJ_SMALL_INT_VALUE(args[0]);
    const char *name = n_args > 1 ? mp_obj_str_get_str(args[1]) : NULL;

#define ESP_PM_LOCK_MAX 3
    if ((unsigned)lock_type >= ESP_PM_LOCK_MAX) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid lock type"));
    }

    esp_pm_lock_handle_t lock;
    check_esp_err(esp_pm_lock_create(lock_type, 0, name, &lock));

    esp32_pm_lock_obj_t *pm_lock = m_new_obj_with_finaliser(esp32_pm_lock_obj_t);
    pm_lock->base.type = type;
    pm_lock->lock = lock;
    pm_lock->name = n_args > 1 ? args[1] : mp_const_none;

    return MP_OBJ_FROM_PTR(pm_lock);
}

STATIC mp_obj_t esp32_pm_lock_acquire(mp_obj_t lock_in) {
    const esp32_pm_lock_obj_t *pm_lock = MP_OBJ_TO_PTR(lock_in);
    esp_err_t err;

    err = esp_pm_lock_acquire(pm_lock->lock);
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Error 0x%04x"), err);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp32_pm_lock_acquire_obj, esp32_pm_lock_acquire);

STATIC mp_obj_t esp32_pm_lock_release(mp_obj_t lock_in) {
    const esp32_pm_lock_obj_t *pm_lock = MP_OBJ_TO_PTR(lock_in);
    esp_err_t err;

    err = esp_pm_lock_release(pm_lock->lock);
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Error 0x%04x"), err);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp32_pm_lock_release_obj, esp32_pm_lock_release);

STATIC mp_obj_t esp32_pm_lock_delete(mp_obj_t lock_in) {
    esp32_pm_lock_obj_t *pm_lock = MP_OBJ_TO_PTR(lock_in);

    if (pm_lock->lock) {
        // Allow deleting acquired lock by releasing it first
        esp_pm_lock_release(pm_lock->lock);

        check_esp_err(esp_pm_lock_delete(pm_lock->lock));
        pm_lock->lock = 0;
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp32_pm_lock_delete_obj, esp32_pm_lock_delete);

STATIC mp_obj_t esp32_pm_lock___exit__(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    return esp32_pm_lock_release(args[0]);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp32_pm_lock___exit___obj, 4, 4, esp32_pm_lock___exit__);

STATIC mp_obj_t esp32_pm_lock___enter__(mp_obj_t lock_in) {
    esp32_pm_lock_acquire(lock_in);
    return lock_in;
}
MP_DEFINE_CONST_FUN_OBJ_1(esp32_pm_lock___enter___obj, esp32_pm_lock___enter__);


STATIC const mp_rom_map_elem_t esp32_pm_lock_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&esp32_pm_lock_delete_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&esp32_pm_lock___exit___obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&esp32_pm_lock___enter___obj) },
    { MP_ROM_QSTR(MP_QSTR_acquire), MP_ROM_PTR(&esp32_pm_lock_acquire_obj) },
    { MP_ROM_QSTR(MP_QSTR_release), MP_ROM_PTR(&esp32_pm_lock_release_obj) },
};
STATIC MP_DEFINE_CONST_DICT(esp32_pm_lock_locals_dict, esp32_pm_lock_locals_dict_table);

STATIC MP_DEFINE_CONST_OBJ_TYPE(
    esp32_pm_lock_type,
    MP_QSTR_Pmlock,
    MP_TYPE_FLAG_NONE,
    make_new, esp32_pm_lock_make_new,
    print, esp32_pm_lock_print,
    locals_dict, &esp32_pm_lock_locals_dict
    );

#endif // CONFIG_PM_ENABLE

STATIC const mp_rom_map_elem_t esp32_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_esp32) },

    { MP_ROM_QSTR(MP_QSTR_wake_on_touch), MP_ROM_PTR(&esp32_wake_on_touch_obj) },
    { MP_ROM_QSTR(MP_QSTR_wake_on_ext0), MP_ROM_PTR(&esp32_wake_on_ext0_obj) },
    { MP_ROM_QSTR(MP_QSTR_wake_on_ext1), MP_ROM_PTR(&esp32_wake_on_ext1_obj) },
    { MP_ROM_QSTR(MP_QSTR_wake_on_ulp), MP_ROM_PTR(&esp32_wake_on_ulp_obj) },
    { MP_ROM_QSTR(MP_QSTR_gpio_deep_sleep_hold), MP_ROM_PTR(&esp32_gpio_deep_sleep_hold_obj) },
    #if CONFIG_IDF_TARGET_ESP32
    { MP_ROM_QSTR(MP_QSTR_raw_temperature), MP_ROM_PTR(&esp32_raw_temperature_obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_idf_heap_info), MP_ROM_PTR(&esp32_idf_heap_info_obj) },

    #if CONFIG_PM_ENABLE
    { MP_ROM_QSTR(MP_QSTR_pm_dump_locks), MP_ROM_PTR(&esp32_pm_dump_locks_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_Pmlock), (mp_obj_t)&esp32_pm_lock_type },
    { MP_ROM_QSTR(MP_QSTR_PM_CPU_FREQ_MAX), MP_ROM_INT(ESP_PM_CPU_FREQ_MAX) },
    { MP_ROM_QSTR(MP_QSTR_PM_APB_FREQ_MAX), MP_ROM_INT(ESP_PM_APB_FREQ_MAX) },
    { MP_ROM_QSTR(MP_QSTR_PM_NO_LIGHTSLEEP), MP_ROM_INT(ESP_PM_NO_LIGHT_SLEEP) },
    #endif

    { MP_ROM_QSTR(MP_QSTR_NVS), MP_ROM_PTR(&esp32_nvs_type) },
    { MP_ROM_QSTR(MP_QSTR_Partition), MP_ROM_PTR(&esp32_partition_type) },
    { MP_ROM_QSTR(MP_QSTR_RMT), MP_ROM_PTR(&esp32_rmt_type) },
    #if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
    { MP_ROM_QSTR(MP_QSTR_ULP), MP_ROM_PTR(&esp32_ulp_type) },
    #endif

    { MP_ROM_QSTR(MP_QSTR_WAKEUP_ALL_LOW), MP_ROM_FALSE },
    { MP_ROM_QSTR(MP_QSTR_WAKEUP_ANY_HIGH), MP_ROM_TRUE },

    { MP_ROM_QSTR(MP_QSTR_HEAP_DATA), MP_ROM_INT(MALLOC_CAP_8BIT) },
    { MP_ROM_QSTR(MP_QSTR_HEAP_EXEC), MP_ROM_INT(MALLOC_CAP_EXEC) },
};

STATIC MP_DEFINE_CONST_DICT(esp32_module_globals, esp32_module_globals_table);

const mp_obj_module_t esp32_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&esp32_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_esp32, esp32_module);
