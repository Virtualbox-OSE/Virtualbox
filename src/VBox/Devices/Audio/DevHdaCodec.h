/* $Id: DevHdaCodec.h $ */
/** @file
 * Intel HD Audio Controller Emulation - Codec, Sigmatel/IDT STAC9220.
 */

/*
 * Copyright (C) 2006-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef VBOX_INCLUDED_SRC_Audio_DevHdaCodec_h
#define VBOX_INCLUDED_SRC_Audio_DevHdaCodec_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/list.h>

#include "AudioMixer.h"

/** Pointer to a shared HDA device state.  */
typedef struct HDASTATE *PHDASTATE;
/** Pointer to a ring-3 HDA device state.  */
typedef struct HDASTATER3 *PHDASTATER3;

/** The ICH HDA (Intel) common codec state. */
typedef struct HDACODEC *PHDACODEC;
/** The ICH HDA (Intel) ring-0 codec state. */
typedef struct HDACODECR0 *PHDACODECR0;
/** The ICH HDA (Intel) ring-3 codec state. */
typedef struct HDACODECR3 *PHDACODECR3;
/** The ICH HDA (Intel) current context codec state. */
typedef CTX_SUFF(PHDACODEC) PHDACODECCC;

/** The HDA host driver backend. */
typedef struct HDADRIVER *PHDADRIVER;

/**
 * Enumeration specifying the codec type to use.
 */
typedef enum CODEC_TYPE
{
    /** Invalid, do not use. */
    CODEC_TYPE_INVALID = 0,
    /** SigmaTel 9220 (922x). */
    CODEC_TYPE_STAC9220,
    /** Hack to blow the type up to 32-bit. */
    CODEC_TYPE__32BIT_HACK = 0x7fffffff
} CODEC_TYPE;

/* PRM 5.3.1 */
/** Codec address mask. */
#define CODEC_CAD_MASK                                     0xF0000000
/** Codec address shift. */
#define CODEC_CAD_SHIFT                                    28
#define CODEC_DIRECT_MASK                                  RT_BIT(27)
/** Node ID mask. */
#define CODEC_NID_MASK                                     0x07F00000
/** Node ID shift. */
#define CODEC_NID_SHIFT                                    20
#define CODEC_VERBDATA_MASK                                0x000FFFFF
#define CODEC_VERB_4BIT_CMD                                0x000FFFF0
#define CODEC_VERB_4BIT_DATA                               0x0000000F
#define CODEC_VERB_8BIT_CMD                                0x000FFF00
#define CODEC_VERB_8BIT_DATA                               0x000000FF
#define CODEC_VERB_16BIT_CMD                               0x000F0000
#define CODEC_VERB_16BIT_DATA                              0x0000FFFF

#define CODEC_CAD(cmd)                                     (((cmd) & CODEC_CAD_MASK) >> CODEC_CAD_SHIFT)
#define CODEC_DIRECT(cmd)                                  ((cmd) & CODEC_DIRECT_MASK)
#define CODEC_NID(cmd)                                     ((((cmd) & CODEC_NID_MASK)) >> CODEC_NID_SHIFT)
#define CODEC_VERBDATA(cmd)                                ((cmd) & CODEC_VERBDATA_MASK)
#define CODEC_VERB_CMD(cmd, mask, x)                       (((cmd) & (mask)) >> (x))
#define CODEC_VERB_CMD4(cmd)                               (CODEC_VERB_CMD((cmd), CODEC_VERB_4BIT_CMD, 4))
#define CODEC_VERB_CMD8(cmd)                               (CODEC_VERB_CMD((cmd), CODEC_VERB_8BIT_CMD, 8))
#define CODEC_VERB_CMD16(cmd)                              (CODEC_VERB_CMD((cmd), CODEC_VERB_16BIT_CMD, 16))
#define CODEC_VERB_PAYLOAD4(cmd)                           ((cmd) & CODEC_VERB_4BIT_DATA)
#define CODEC_VERB_PAYLOAD8(cmd)                           ((cmd) & CODEC_VERB_8BIT_DATA)
#define CODEC_VERB_PAYLOAD16(cmd)                          ((cmd) & CODEC_VERB_16BIT_DATA)

#define CODEC_VERB_GET_AMP_DIRECTION                       RT_BIT(15)
#define CODEC_VERB_GET_AMP_SIDE                            RT_BIT(13)
#define CODEC_VERB_GET_AMP_INDEX                           0x7

/* HDA spec 7.3.3.7 NoteA */
#define CODEC_GET_AMP_DIRECTION(cmd)                       (((cmd) & CODEC_VERB_GET_AMP_DIRECTION) >> 15)
#define CODEC_GET_AMP_SIDE(cmd)                            (((cmd) & CODEC_VERB_GET_AMP_SIDE) >> 13)
#define CODEC_GET_AMP_INDEX(cmd)                           (CODEC_GET_AMP_DIRECTION(cmd) ? 0 : ((cmd) & CODEC_VERB_GET_AMP_INDEX))

/* HDA spec 7.3.3.7 NoteC */
#define CODEC_VERB_SET_AMP_OUT_DIRECTION                   RT_BIT(15)
#define CODEC_VERB_SET_AMP_IN_DIRECTION                    RT_BIT(14)
#define CODEC_VERB_SET_AMP_LEFT_SIDE                       RT_BIT(13)
#define CODEC_VERB_SET_AMP_RIGHT_SIDE                      RT_BIT(12)
#define CODEC_VERB_SET_AMP_INDEX                           (0x7 << 8)
#define CODEC_VERB_SET_AMP_MUTE                            RT_BIT(7)
/** Note: 7-bit value [6:0]. */
#define CODEC_VERB_SET_AMP_GAIN                            0x7F

#define CODEC_SET_AMP_IS_OUT_DIRECTION(cmd)                (((cmd) & CODEC_VERB_SET_AMP_OUT_DIRECTION) != 0)
#define CODEC_SET_AMP_IS_IN_DIRECTION(cmd)                 (((cmd) & CODEC_VERB_SET_AMP_IN_DIRECTION) != 0)
#define CODEC_SET_AMP_IS_LEFT_SIDE(cmd)                    (((cmd) & CODEC_VERB_SET_AMP_LEFT_SIDE) != 0)
#define CODEC_SET_AMP_IS_RIGHT_SIDE(cmd)                   (((cmd) & CODEC_VERB_SET_AMP_RIGHT_SIDE) != 0)
#define CODEC_SET_AMP_INDEX(cmd)                           (((cmd) & CODEC_VERB_SET_AMP_INDEX) >> 7)
#define CODEC_SET_AMP_MUTE(cmd)                            ((cmd) & CODEC_VERB_SET_AMP_MUTE)
#define CODEC_SET_AMP_GAIN(cmd)                            ((cmd) & CODEC_VERB_SET_AMP_GAIN)

/* HDA spec 7.3.3.1 defines layout of configuration registers/verbs (0xF00) */
/* VendorID (7.3.4.1) */
#define CODEC_MAKE_F00_00(vendorID, deviceID)              (((vendorID) << 16) | (deviceID))
#define CODEC_F00_00_VENDORID(f00_00)                      (((f00_00) >> 16) & 0xFFFF)
#define CODEC_F00_00_DEVICEID(f00_00)                      ((f00_00) & 0xFFFF)

/** RevisionID (7.3.4.2). */
#define CODEC_MAKE_F00_02(majRev, minRev, venFix, venProg, stepFix, stepProg) \
    (  (((majRev)   & 0xF) << 20) \
     | (((minRev)   & 0xF) << 16) \
     | (((venFix)   & 0xF) << 12) \
     | (((venProg)  & 0xF) << 8)  \
     | (((stepFix)  & 0xF) << 4)  \
     |  ((stepProg) & 0xF))

/** Subordinate node count (7.3.4.3). */
#define CODEC_MAKE_F00_04(startNodeNumber, totalNodeNumber) ((((startNodeNumber) & 0xFF) << 16)|((totalNodeNumber) & 0xFF))
#define CODEC_F00_04_TO_START_NODE_NUMBER(f00_04)          (((f00_04) >> 16) & 0xFF)
#define CODEC_F00_04_TO_NODE_COUNT(f00_04)                 ((f00_04) & 0xFF)
/*
 * Function Group Type  (7.3.4.4)
 * 0 & [0x3-0x7f] are reserved types
 * [0x80 - 0xff] are vendor defined function groups
 */
#define CODEC_MAKE_F00_05(UnSol, NodeType)                 (((UnSol) << 8)|(NodeType))
#define CODEC_F00_05_UNSOL                                 RT_BIT(8)
#define CODEC_F00_05_AFG                                   (0x1)
#define CODEC_F00_05_MFG                                   (0x2)
#define CODEC_F00_05_IS_UNSOL(f00_05)                      RT_BOOL((f00_05) & RT_BIT(8))
#define CODEC_F00_05_GROUP(f00_05)                         ((f00_05) & 0xff)
/* Audio Function Group capabilities (7.3.4.5). */
#define CODEC_MAKE_F00_08(BeepGen, InputDelay, OutputDelay) ((((BeepGen) & 0x1) << 16)| (((InputDelay) & 0xF) << 8) | ((OutputDelay) & 0xF))
#define CODEC_F00_08_BEEP_GEN(f00_08)                      ((f00_08) & RT_BIT(16)

/* Converter Stream, Channel (7.3.3.11). */
#define CODEC_F00_06_GET_STREAM_ID(cmd)                    (((cmd) >> 4) & 0x0F)
#define CODEC_F00_06_GET_CHANNEL_ID(cmd)                   (((cmd) & 0x0F))

/* Widget Capabilities (7.3.4.6). */
#define CODEC_MAKE_F00_09(type, delay, chan_ext) \
    ( (((type)     & 0xF) << 20)            \
    | (((delay)    & 0xF) << 16)           \
    | (((chan_ext) & 0xF) << 13))
/* note: types 0x8-0xe are reserved */
#define CODEC_F00_09_TYPE_AUDIO_OUTPUT                     (0x0)
#define CODEC_F00_09_TYPE_AUDIO_INPUT                      (0x1)
#define CODEC_F00_09_TYPE_AUDIO_MIXER                      (0x2)
#define CODEC_F00_09_TYPE_AUDIO_SELECTOR                   (0x3)
#define CODEC_F00_09_TYPE_PIN_COMPLEX                      (0x4)
#define CODEC_F00_09_TYPE_POWER_WIDGET                     (0x5)
#define CODEC_F00_09_TYPE_VOLUME_KNOB                      (0x6)
#define CODEC_F00_09_TYPE_BEEP_GEN                         (0x7)
#define CODEC_F00_09_TYPE_VENDOR_DEFINED                   (0xF)

#define CODEC_F00_09_CAP_CP                                RT_BIT(12)
#define CODEC_F00_09_CAP_L_R_SWAP                          RT_BIT(11)
#define CODEC_F00_09_CAP_POWER_CTRL                        RT_BIT(10)
#define CODEC_F00_09_CAP_DIGITAL                           RT_BIT(9)
#define CODEC_F00_09_CAP_CONNECTION_LIST                   RT_BIT(8)
#define CODEC_F00_09_CAP_UNSOL                             RT_BIT(7)
#define CODEC_F00_09_CAP_PROC_WIDGET                       RT_BIT(6)
#define CODEC_F00_09_CAP_STRIPE                            RT_BIT(5)
#define CODEC_F00_09_CAP_FMT_OVERRIDE                      RT_BIT(4)
#define CODEC_F00_09_CAP_AMP_FMT_OVERRIDE                  RT_BIT(3)
#define CODEC_F00_09_CAP_OUT_AMP_PRESENT                   RT_BIT(2)
#define CODEC_F00_09_CAP_IN_AMP_PRESENT                    RT_BIT(1)
#define CODEC_F00_09_CAP_STEREO                            RT_BIT(0)

#define CODEC_F00_09_TYPE(f00_09)                          (((f00_09) >> 20) & 0xF)

#define CODEC_F00_09_IS_CAP_CP(f00_09)                     RT_BOOL((f00_09) & RT_BIT(12))
#define CODEC_F00_09_IS_CAP_L_R_SWAP(f00_09)               RT_BOOL((f00_09) & RT_BIT(11))
#define CODEC_F00_09_IS_CAP_POWER_CTRL(f00_09)             RT_BOOL((f00_09) & RT_BIT(10))
#define CODEC_F00_09_IS_CAP_DIGITAL(f00_09)                RT_BOOL((f00_09) & RT_BIT(9))
#define CODEC_F00_09_IS_CAP_CONNECTION_LIST(f00_09)        RT_BOOL((f00_09) & RT_BIT(8))
#define CODEC_F00_09_IS_CAP_UNSOL(f00_09)                  RT_BOOL((f00_09) & RT_BIT(7))
#define CODEC_F00_09_IS_CAP_PROC_WIDGET(f00_09)            RT_BOOL((f00_09) & RT_BIT(6))
#define CODEC_F00_09_IS_CAP_STRIPE(f00_09)                 RT_BOOL((f00_09) & RT_BIT(5))
#define CODEC_F00_09_IS_CAP_FMT_OVERRIDE(f00_09)           RT_BOOL((f00_09) & RT_BIT(4))
#define CODEC_F00_09_IS_CAP_AMP_OVERRIDE(f00_09)           RT_BOOL((f00_09) & RT_BIT(3))
#define CODEC_F00_09_IS_CAP_OUT_AMP_PRESENT(f00_09)        RT_BOOL((f00_09) & RT_BIT(2))
#define CODEC_F00_09_IS_CAP_IN_AMP_PRESENT(f00_09)         RT_BOOL((f00_09) & RT_BIT(1))
#define CODEC_F00_09_IS_CAP_LSB(f00_09)                    RT_BOOL((f00_09) & RT_BIT(0))

/* Supported PCM size, rates (7.3.4.7) */
#define CODEC_F00_0A_32_BIT                                RT_BIT(19)
#define CODEC_F00_0A_24_BIT                                RT_BIT(18)
#define CODEC_F00_0A_16_BIT                                RT_BIT(17)
#define CODEC_F00_0A_8_BIT                                 RT_BIT(16)

#define CODEC_F00_0A_48KHZ_MULT_8X                         RT_BIT(11)
#define CODEC_F00_0A_48KHZ_MULT_4X                         RT_BIT(10)
#define CODEC_F00_0A_44_1KHZ_MULT_4X                       RT_BIT(9)
#define CODEC_F00_0A_48KHZ_MULT_2X                         RT_BIT(8)
#define CODEC_F00_0A_44_1KHZ_MULT_2X                       RT_BIT(7)
#define CODEC_F00_0A_48KHZ                                 RT_BIT(6)
#define CODEC_F00_0A_44_1KHZ                               RT_BIT(5)
/* 2/3 * 48kHz */
#define CODEC_F00_0A_48KHZ_2_3X                            RT_BIT(4)
/* 1/2 * 44.1kHz */
#define CODEC_F00_0A_44_1KHZ_1_2X                          RT_BIT(3)
/* 1/3 * 48kHz */
#define CODEC_F00_0A_48KHZ_1_3X                            RT_BIT(2)
/* 1/4 * 44.1kHz */
#define CODEC_F00_0A_44_1KHZ_1_4X                          RT_BIT(1)
/* 1/6 * 48kHz */
#define CODEC_F00_0A_48KHZ_1_6X                            RT_BIT(0)

/* Supported streams formats (7.3.4.8) */
#define CODEC_F00_0B_AC3                                   RT_BIT(2)
#define CODEC_F00_0B_FLOAT32                               RT_BIT(1)
#define CODEC_F00_0B_PCM                                   RT_BIT(0)

/* Pin Capabilities (7.3.4.9)*/
#define CODEC_MAKE_F00_0C(vref_ctrl) (((vref_ctrl) & 0xFF) << 8)
#define CODEC_F00_0C_CAP_HBR                               RT_BIT(27)
#define CODEC_F00_0C_CAP_DP                                RT_BIT(24)
#define CODEC_F00_0C_CAP_EAPD                              RT_BIT(16)
#define CODEC_F00_0C_CAP_HDMI                              RT_BIT(7)
#define CODEC_F00_0C_CAP_BALANCED_IO                       RT_BIT(6)
#define CODEC_F00_0C_CAP_INPUT                             RT_BIT(5)
#define CODEC_F00_0C_CAP_OUTPUT                            RT_BIT(4)
#define CODEC_F00_0C_CAP_HEADPHONE_AMP                     RT_BIT(3)
#define CODEC_F00_0C_CAP_PRESENCE_DETECT                   RT_BIT(2)
#define CODEC_F00_0C_CAP_TRIGGER_REQUIRED                  RT_BIT(1)
#define CODEC_F00_0C_CAP_IMPENDANCE_SENSE                  RT_BIT(0)

#define CODEC_F00_0C_IS_CAP_HBR(f00_0c)                    ((f00_0c) & RT_BIT(27))
#define CODEC_F00_0C_IS_CAP_DP(f00_0c)                     ((f00_0c) & RT_BIT(24))
#define CODEC_F00_0C_IS_CAP_EAPD(f00_0c)                   ((f00_0c) & RT_BIT(16))
#define CODEC_F00_0C_IS_CAP_HDMI(f00_0c)                   ((f00_0c) & RT_BIT(7))
#define CODEC_F00_0C_IS_CAP_BALANCED_IO(f00_0c)            ((f00_0c) & RT_BIT(6))
#define CODEC_F00_0C_IS_CAP_INPUT(f00_0c)                  ((f00_0c) & RT_BIT(5))
#define CODEC_F00_0C_IS_CAP_OUTPUT(f00_0c)                 ((f00_0c) & RT_BIT(4))
#define CODEC_F00_0C_IS_CAP_HP(f00_0c)                     ((f00_0c) & RT_BIT(3))
#define CODEC_F00_0C_IS_CAP_PRESENCE_DETECT(f00_0c)        ((f00_0c) & RT_BIT(2))
#define CODEC_F00_0C_IS_CAP_TRIGGER_REQUIRED(f00_0c)       ((f00_0c) & RT_BIT(1))
#define CODEC_F00_0C_IS_CAP_IMPENDANCE_SENSE(f00_0c)       ((f00_0c) & RT_BIT(0))

/* Input Amplifier capabilities (7.3.4.10). */
#define CODEC_MAKE_F00_0D(mute_cap, step_size, num_steps, offset) \
        (  (((mute_cap)  & UINT32_C(0x1))  << 31) \
         | (((step_size) & UINT32_C(0xFF)) << 16) \
         | (((num_steps) & UINT32_C(0xFF)) << 8) \
         |  ((offset)    & UINT32_C(0xFF)))

#define CODEC_F00_0D_CAP_MUTE                              RT_BIT(7)

#define CODEC_F00_0D_IS_CAP_MUTE(f00_0d)                   ( ( f00_0d) & RT_BIT(31))
#define CODEC_F00_0D_STEP_SIZE(f00_0d)                     ((( f00_0d) & (0x7F << 16)) >> 16)
#define CODEC_F00_0D_NUM_STEPS(f00_0d)                     ((((f00_0d) & (0x7F << 8)) >> 8) + 1)
#define CODEC_F00_0D_OFFSET(f00_0d)                        (  (f00_0d) & 0x7F)

/** Indicates that the amplifier can be muted. */
#define CODEC_AMP_CAP_MUTE                                 0x1
/** The amplifier's maximum number of steps. We want
 *  a ~90dB dynamic range, so 64 steps with 1.25dB each
 *  should do the trick.
 *
 *  As we want to map our range to [0..128] values we can avoid
 *  multiplication and simply doing a shift later.
 *
 *  Produces -96dB to +0dB.
 *  "0" indicates a step of 0.25dB, "127" indicates a step of 32dB.
 */
#define CODEC_AMP_NUM_STEPS                                0x7F
/** The initial gain offset (and when doing a node reset). */
#define CODEC_AMP_OFF_INITIAL                              0x7F
/** The amplifier's gain step size. */
#define CODEC_AMP_STEP_SIZE                                0x2

/* Output Amplifier capabilities (7.3.4.10) */
#define CODEC_MAKE_F00_12                                  CODEC_MAKE_F00_0D

#define CODEC_F00_12_IS_CAP_MUTE(f00_12)                   CODEC_F00_0D_IS_CAP_MUTE(f00_12)
#define CODEC_F00_12_STEP_SIZE(f00_12)                     CODEC_F00_0D_STEP_SIZE(f00_12)
#define CODEC_F00_12_NUM_STEPS(f00_12)                     CODEC_F00_0D_NUM_STEPS(f00_12)
#define CODEC_F00_12_OFFSET(f00_12)                        CODEC_F00_0D_OFFSET(f00_12)

/* Connection list lenght (7.3.4.11). */
#define CODEC_MAKE_F00_0E(long_form, length)    \
    (  (((long_form) & 0x1) << 7)               \
     | ((length) & 0x7F))
/* Indicates short-form NIDs. */
#define CODEC_F00_0E_LIST_NID_SHORT                        0
/* Indicates long-form NIDs. */
#define CODEC_F00_0E_LIST_NID_LONG                         1
#define CODEC_F00_0E_IS_LONG(f00_0e)                       RT_BOOL((f00_0e) & RT_BIT(7))
#define CODEC_F00_0E_COUNT(f00_0e)                         ((f00_0e) & 0x7F)
/* Supported Power States (7.3.4.12) */
#define CODEC_F00_0F_EPSS                                  RT_BIT(31)
#define CODEC_F00_0F_CLKSTOP                               RT_BIT(30)
#define CODEC_F00_0F_S3D3                                  RT_BIT(29)
#define CODEC_F00_0F_D3COLD                                RT_BIT(4)
#define CODEC_F00_0F_D3                                    RT_BIT(3)
#define CODEC_F00_0F_D2                                    RT_BIT(2)
#define CODEC_F00_0F_D1                                    RT_BIT(1)
#define CODEC_F00_0F_D0                                    RT_BIT(0)

/* Processing capabilities 7.3.4.13 */
#define CODEC_MAKE_F00_10(num, benign)                     ((((num) & 0xFF) << 8) | ((benign) & 0x1))
#define CODEC_F00_10_NUM(f00_10)                           (((f00_10) & (0xFF << 8)) >> 8)
#define CODEC_F00_10_BENING(f00_10)                        ((f00_10) & 0x1)

/* GPIO count (7.3.4.14). */
#define CODEC_MAKE_F00_11(wake, unsol, numgpi, numgpo, numgpio) \
    (  (((wake)   & UINT32_C(0x1))  << 31) \
     | (((unsol)  & UINT32_C(0x1))  << 30) \
     | (((numgpi) & UINT32_C(0xFF)) << 16) \
     | (((numgpo) & UINT32_C(0xFF)) << 8) \
     | ((numgpio) & UINT32_C(0xFF)))

/* Processing States (7.3.3.4). */
#define CODEC_F03_OFF                                      (0)
#define CODEC_F03_ON                                       RT_BIT(0)
#define CODEC_F03_BENING                                   RT_BIT(1)
/* Power States (7.3.3.10). */
#define CODEC_MAKE_F05(reset, stopok, error, act, set) \
    (  (((reset)  & 0x1) << 10) \
     | (((stopok) & 0x1) << 9) \
     | (((error)  & 0x1) << 8) \
     | (((act)    & 0xF) << 4) \
     | ((set)     & 0xF))
#define CODEC_F05_D3COLD                                   (4)
#define CODEC_F05_D3                                       (3)
#define CODEC_F05_D2                                       (2)
#define CODEC_F05_D1                                       (1)
#define CODEC_F05_D0                                       (0)

#define CODEC_F05_IS_RESET(value)                          (((value) & RT_BIT(10)) != 0)
#define CODEC_F05_IS_STOPOK(value)                         (((value) & RT_BIT(9)) != 0)
#define CODEC_F05_IS_ERROR(value)                          (((value) & RT_BIT(8)) != 0)
#define CODEC_F05_ACT(value)                               (((value) & 0xF0) >> 4)
#define CODEC_F05_SET(value)                               (((value) & 0xF))

#define CODEC_F05_GE(p0, p1)                               ((p0) <= (p1))
#define CODEC_F05_LE(p0, p1)                               ((p0) >= (p1))

/* Converter Stream, Channel (7.3.3.11). */
#define CODEC_MAKE_F06(stream, channel) \
    (  (((stream)  & 0xF) << 4)         \
     |  ((channel) & 0xF))
#define CODEC_F06_STREAM(value)                            ((value) & 0xF0)
#define CODEC_F06_CHANNEL(value)                           ((value) & 0xF)

/* Pin Widged Control (7.3.3.13). */
#define CODEC_F07_VREF_HIZ                                 (0)
#define CODEC_F07_VREF_50                                  (0x1)
#define CODEC_F07_VREF_GROUND                              (0x2)
#define CODEC_F07_VREF_80                                  (0x4)
#define CODEC_F07_VREF_100                                 (0x5)
#define CODEC_F07_IN_ENABLE                                RT_BIT(5)
#define CODEC_F07_OUT_ENABLE                               RT_BIT(6)
#define CODEC_F07_OUT_H_ENABLE                             RT_BIT(7)

/* Volume Knob Control (7.3.3.29). */
#define CODEC_F0F_IS_DIRECT                                RT_BIT(7)
#define CODEC_F0F_VOLUME                                   (0x7F)

/* Unsolicited enabled (7.3.3.14). */
#define CODEC_MAKE_F08(enable, tag) ((((enable) & 1) << 7) | ((tag) & 0x3F))

/* Converter formats (7.3.3.8) and (3.7.1). */
/* This is the same format as SDnFMT. */
#define CODEC_MAKE_A                                       HDA_SDFMT_MAKE

#define CODEC_A_TYPE                                       HDA_SDFMT_TYPE
#define CODEC_A_TYPE_PCM                                   HDA_SDFMT_TYPE_PCM
#define CODEC_A_TYPE_NON_PCM                               HDA_SDFMT_TYPE_NON_PCM

#define CODEC_A_BASE                                       HDA_SDFMT_BASE
#define CODEC_A_BASE_48KHZ                                 HDA_SDFMT_BASE_48KHZ
#define CODEC_A_BASE_44KHZ                                 HDA_SDFMT_BASE_44KHZ

/* Pin Sense (7.3.3.15). */
#define CODEC_MAKE_F09_ANALOG(fPresent, impedance)  \
(  (((fPresent) & 0x1) << 31)                       \
 | (((impedance) & UINT32_C(0x7FFFFFFF))))
#define CODEC_F09_ANALOG_NA    UINT32_C(0x7FFFFFFF)
#define CODEC_MAKE_F09_DIGITAL(fPresent, fELDValid) \
(   (((fPresent)  & UINT32_C(0x1)) << 31)                      \
  | (((fELDValid) & UINT32_C(0x1)) << 30))

#define CODEC_MAKE_F0C(lrswap, eapd, btl) ((((lrswap) & 1) << 2) | (((eapd) & 1) << 1) | ((btl) & 1))
#define CODEC_FOC_IS_LRSWAP(f0c)                           RT_BOOL((f0c) & RT_BIT(2))
#define CODEC_FOC_IS_EAPD(f0c)                             RT_BOOL((f0c) & RT_BIT(1))
#define CODEC_FOC_IS_BTL(f0c)                              RT_BOOL((f0c) & RT_BIT(0))
/* HDA spec 7.3.3.31 defines layout of configuration registers/verbs (0xF1C) */
/* Configuration's port connection */
#define CODEC_F1C_PORT_MASK                                (0x3)
#define CODEC_F1C_PORT_SHIFT                               (30)

#define CODEC_F1C_PORT_COMPLEX                             (0x0)
#define CODEC_F1C_PORT_NO_PHYS                             (0x1)
#define CODEC_F1C_PORT_FIXED                               (0x2)
#define CODEC_F1C_BOTH                                     (0x3)

/* Configuration default: connection */
#define CODEC_F1C_PORT_MASK                                (0x3)
#define CODEC_F1C_PORT_SHIFT                               (30)

/* Connected to a jack (1/8", ATAPI, ...). */
#define CODEC_F1C_PORT_COMPLEX                             (0x0)
/* No physical connection. */
#define CODEC_F1C_PORT_NO_PHYS                             (0x1)
/* Fixed function device (integrated speaker, integrated mic, ...). */
#define CODEC_F1C_PORT_FIXED                               (0x2)
/* Both, a jack and an internal device are attached. */
#define CODEC_F1C_BOTH                                     (0x3)

/* Configuration default: Location */
#define CODEC_F1C_LOCATION_MASK                            (0x3F)
#define CODEC_F1C_LOCATION_SHIFT                           (24)

/* [4:5] bits of location region means chassis attachment */
#define CODEC_F1C_LOCATION_PRIMARY_CHASSIS                 (0)
#define CODEC_F1C_LOCATION_INTERNAL                        RT_BIT(4)
#define CODEC_F1C_LOCATION_SECONDRARY_CHASSIS              RT_BIT(5)
#define CODEC_F1C_LOCATION_OTHER                           RT_BIT(5)

/* [0:3] bits of location region means geometry location attachment */
#define CODEC_F1C_LOCATION_NA                              (0)
#define CODEC_F1C_LOCATION_REAR                            (0x1)
#define CODEC_F1C_LOCATION_FRONT                           (0x2)
#define CODEC_F1C_LOCATION_LEFT                            (0x3)
#define CODEC_F1C_LOCATION_RIGTH                           (0x4)
#define CODEC_F1C_LOCATION_TOP                             (0x5)
#define CODEC_F1C_LOCATION_BOTTOM                          (0x6)
#define CODEC_F1C_LOCATION_SPECIAL_0                       (0x7)
#define CODEC_F1C_LOCATION_SPECIAL_1                       (0x8)
#define CODEC_F1C_LOCATION_SPECIAL_2                       (0x9)

/* Configuration default: Device type */
#define CODEC_F1C_DEVICE_MASK                              (0xF)
#define CODEC_F1C_DEVICE_SHIFT                             (20)
#define CODEC_F1C_DEVICE_LINE_OUT                          (0)
#define CODEC_F1C_DEVICE_SPEAKER                           (0x1)
#define CODEC_F1C_DEVICE_HP                                (0x2)
#define CODEC_F1C_DEVICE_CD                                (0x3)
#define CODEC_F1C_DEVICE_SPDIF_OUT                         (0x4)
#define CODEC_F1C_DEVICE_DIGITAL_OTHER_OUT                 (0x5)
#define CODEC_F1C_DEVICE_MODEM_LINE_SIDE                   (0x6)
#define CODEC_F1C_DEVICE_MODEM_HANDSET_SIDE                (0x7)
#define CODEC_F1C_DEVICE_LINE_IN                           (0x8)
#define CODEC_F1C_DEVICE_AUX                               (0x9)
#define CODEC_F1C_DEVICE_MIC                               (0xA)
#define CODEC_F1C_DEVICE_PHONE                             (0xB)
#define CODEC_F1C_DEVICE_SPDIF_IN                          (0xC)
#define CODEC_F1C_DEVICE_RESERVED                          (0xE)
#define CODEC_F1C_DEVICE_OTHER                             (0xF)

/* Configuration default: Connection type */
#define CODEC_F1C_CONNECTION_TYPE_MASK                     (0xF)
#define CODEC_F1C_CONNECTION_TYPE_SHIFT                    (16)

#define CODEC_F1C_CONNECTION_TYPE_UNKNOWN                  (0)
#define CODEC_F1C_CONNECTION_TYPE_1_8INCHES                (0x1)
#define CODEC_F1C_CONNECTION_TYPE_1_4INCHES                (0x2)
#define CODEC_F1C_CONNECTION_TYPE_ATAPI                    (0x3)
#define CODEC_F1C_CONNECTION_TYPE_RCA                      (0x4)
#define CODEC_F1C_CONNECTION_TYPE_OPTICAL                  (0x5)
#define CODEC_F1C_CONNECTION_TYPE_OTHER_DIGITAL            (0x6)
#define CODEC_F1C_CONNECTION_TYPE_ANALOG                   (0x7)
#define CODEC_F1C_CONNECTION_TYPE_DIN                      (0x8)
#define CODEC_F1C_CONNECTION_TYPE_XLR                      (0x9)
#define CODEC_F1C_CONNECTION_TYPE_RJ_11                    (0xA)
#define CODEC_F1C_CONNECTION_TYPE_COMBO                    (0xB)
#define CODEC_F1C_CONNECTION_TYPE_OTHER                    (0xF)

/* Configuration's color */
#define CODEC_F1C_COLOR_MASK                               (0xF)
#define CODEC_F1C_COLOR_SHIFT                              (12)
#define CODEC_F1C_COLOR_UNKNOWN                            (0)
#define CODEC_F1C_COLOR_BLACK                              (0x1)
#define CODEC_F1C_COLOR_GREY                               (0x2)
#define CODEC_F1C_COLOR_BLUE                               (0x3)
#define CODEC_F1C_COLOR_GREEN                              (0x4)
#define CODEC_F1C_COLOR_RED                                (0x5)
#define CODEC_F1C_COLOR_ORANGE                             (0x6)
#define CODEC_F1C_COLOR_YELLOW                             (0x7)
#define CODEC_F1C_COLOR_PURPLE                             (0x8)
#define CODEC_F1C_COLOR_PINK                               (0x9)
#define CODEC_F1C_COLOR_RESERVED_0                         (0xA)
#define CODEC_F1C_COLOR_RESERVED_1                         (0xB)
#define CODEC_F1C_COLOR_RESERVED_2                         (0xC)
#define CODEC_F1C_COLOR_RESERVED_3                         (0xD)
#define CODEC_F1C_COLOR_WHITE                              (0xE)
#define CODEC_F1C_COLOR_OTHER                              (0xF)

/* Configuration's misc */
#define CODEC_F1C_MISC_MASK                                (0xF)
#define CODEC_F1C_MISC_SHIFT                               (8)
#define CODEC_F1C_MISC_NONE                                0
#define CODEC_F1C_MISC_JACK_NO_PRESENCE_DETECT             RT_BIT(0)
#define CODEC_F1C_MISC_RESERVED_0                          RT_BIT(1)
#define CODEC_F1C_MISC_RESERVED_1                          RT_BIT(2)
#define CODEC_F1C_MISC_RESERVED_2                          RT_BIT(3)

/* Configuration default: Association */
#define CODEC_F1C_ASSOCIATION_MASK                         (0xF)
#define CODEC_F1C_ASSOCIATION_SHIFT                        (4)

/** Reserved; don't use. */
#define CODEC_F1C_ASSOCIATION_INVALID                      0x0
#define CODEC_F1C_ASSOCIATION_GROUP_0                      0x1
#define CODEC_F1C_ASSOCIATION_GROUP_1                      0x2
#define CODEC_F1C_ASSOCIATION_GROUP_2                      0x3
#define CODEC_F1C_ASSOCIATION_GROUP_3                      0x4
#define CODEC_F1C_ASSOCIATION_GROUP_4                      0x5
#define CODEC_F1C_ASSOCIATION_GROUP_5                      0x6
#define CODEC_F1C_ASSOCIATION_GROUP_6                      0x7
#define CODEC_F1C_ASSOCIATION_GROUP_7                      0x8
/* Note: Windows OSes will treat group 15 (0xF) as single PIN devices.
 *       The sequence number associated with that group then will be ignored. */
#define CODEC_F1C_ASSOCIATION_GROUP_15                     0xF

/* Configuration default: Association Sequence. */
#define CODEC_F1C_SEQ_MASK                                 (0xF)
#define CODEC_F1C_SEQ_SHIFT                                (0)

/* Implementation identification (7.3.3.30). */
#define CODEC_MAKE_F20(bmid, bsku, aid)     \
    (  (((bmid) & 0xFFFF) << 16)            \
     | (((bsku) & 0xFF) << 8)               \
     | (((aid) & 0xFF))                     \
    )

/* Macro definition helping in filling the configuration registers. */
#define CODEC_MAKE_F1C(port_connectivity, location, device, connection_type, color, misc, association, sequence)    \
    (  (((port_connectivity) & 0xF) << CODEC_F1C_PORT_SHIFT)            \
     | (((location)          & 0xF) << CODEC_F1C_LOCATION_SHIFT)        \
     | (((device)            & 0xF) << CODEC_F1C_DEVICE_SHIFT)          \
     | (((connection_type)   & 0xF) << CODEC_F1C_CONNECTION_TYPE_SHIFT) \
     | (((color)             & 0xF) << CODEC_F1C_COLOR_SHIFT)           \
     | (((misc)              & 0xF) << CODEC_F1C_MISC_SHIFT)            \
     | (((association)       & 0xF) << CODEC_F1C_ASSOCIATION_SHIFT)     \
     | (((sequence)          & 0xF)))


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** The F00 parameter length (in dwords). */
#define CODECNODE_F00_PARAM_LENGTH  20
/** The F02 parameter length (in dwords). */
#define CODECNODE_F02_PARAM_LENGTH  16

/* PRM 5.3.1 */
#define CODEC_RESPONSE_UNSOLICITED RT_BIT_64(34)

/**
 * A codec verb descriptor.
 */
typedef struct CODECVERB
{
    /** Verb. */
    uint32_t                   uVerb;
    /** Verb mask. */
    uint32_t                   fMask;
    /**
     * Function pointer for implementation callback.
     *
     * This is always a valid  pointer in ring-3, while elsewhere a NULL indicates
     * that we must return to ring-3 to process it.
     */
    DECLCALLBACKMEMBER(int,    pfn)(PHDACODEC pThis, PHDACODECCC pThisCC, uint32_t uCmd, uint64_t *puResp);
    /** Friendly name, for debugging. */
    const char                *pszName;
} CODECVERB;
/** Pointer to a const codec verb descriptor. */
typedef CODECVERB const *PCCODECVERB;


#define AMPLIFIER_SIZE 60

typedef uint32_t AMPLIFIER[AMPLIFIER_SIZE];

/**
 * Common (or core) codec node structure.
 */
typedef struct CODECCOMMONNODE
{
    /** The node's ID. */
    uint8_t         uID;
    /** The node's name. */
    /** The SDn ID this node is assigned to.
     *  0 means not assigned, 1 is SDn0. */
    uint8_t         uSD;
    /** The SDn's channel to use.
     *  Only valid if a valid SDn ID is set. */
    uint8_t         uChannel;
    /* PRM 5.3.6 */
    uint32_t        au32F00_param[CODECNODE_F00_PARAM_LENGTH];
    uint32_t        au32F02_param[CODECNODE_F02_PARAM_LENGTH];
} CODECCOMMONNODE;
AssertCompile(CODECNODE_F00_PARAM_LENGTH == 20);  /* saved state */
AssertCompile(CODECNODE_F02_PARAM_LENGTH == 16); /* saved state */
AssertCompileSize(CODECCOMMONNODE, (1 + 20 + 16) * sizeof(uint32_t));
typedef CODECCOMMONNODE *PCODECCOMMONNODE;

/**
 * Compile time assertion on the expected node size.
 */
#define AssertNodeSize(a_Node, a_cParams) \
    AssertCompile((a_cParams) <= (60 + 6)); /* the max size - saved state */ \
    AssertCompile(   sizeof(a_Node) - sizeof(CODECCOMMONNODE)  \
                  == ((a_cParams) * sizeof(uint32_t)) )

typedef struct ROOTCODECNODE
{
    CODECCOMMONNODE node;
} ROOTCODECNODE, *PROOTCODECNODE;
AssertNodeSize(ROOTCODECNODE, 0);

typedef struct DACNODE
{
    CODECCOMMONNODE node;
    uint32_t    u32F0d_param;
    uint32_t    u32F04_param;
    uint32_t    u32F05_param;
    uint32_t    u32F06_param;
    uint32_t    u32F0c_param;

    uint32_t    u32A_param;
    AMPLIFIER   B_params;

} DACNODE, *PDACNODE;
AssertNodeSize(DACNODE, 6 + 60);

typedef struct ADCNODE
{
    CODECCOMMONNODE node;
    uint32_t    u32F01_param;
    uint32_t    u32F03_param;
    uint32_t    u32F05_param;
    uint32_t    u32F06_param;
    uint32_t    u32F09_param;

    uint32_t    u32A_param;
    AMPLIFIER   B_params;
} ADCNODE, *PADCNODE;
AssertNodeSize(DACNODE, 6 + 60);

typedef struct SPDIFOUTNODE
{
    CODECCOMMONNODE node;
    uint32_t    u32F05_param;
    uint32_t    u32F06_param;
    uint32_t    u32F09_param;
    uint32_t    u32F0d_param;

    uint32_t    u32A_param;
    AMPLIFIER   B_params;
} SPDIFOUTNODE, *PSPDIFOUTNODE;
AssertNodeSize(SPDIFOUTNODE, 5 + 60);

typedef struct SPDIFINNODE
{
    CODECCOMMONNODE node;
    uint32_t    u32F05_param;
    uint32_t    u32F06_param;
    uint32_t    u32F09_param;
    uint32_t    u32F0d_param;

    uint32_t    u32A_param;
    AMPLIFIER   B_params;
} SPDIFINNODE, *PSPDIFINNODE;
AssertNodeSize(SPDIFINNODE, 5 + 60);

typedef struct AFGCODECNODE
{
    CODECCOMMONNODE node;
    uint32_t  u32F05_param;
    uint32_t  u32F08_param;
    uint32_t  u32F17_param;
    uint32_t  u32F20_param;
} AFGCODECNODE, *PAFGCODECNODE;
AssertNodeSize(AFGCODECNODE, 4);

typedef struct PORTNODE
{
    CODECCOMMONNODE node;
    uint32_t u32F01_param;
    uint32_t u32F07_param;
    uint32_t u32F08_param;
    uint32_t u32F09_param;
    uint32_t u32F1c_param;
    AMPLIFIER   B_params;
} PORTNODE, *PPORTNODE;
AssertNodeSize(PORTNODE, 5 + 60);

typedef struct DIGOUTNODE
{
    CODECCOMMONNODE node;
    uint32_t u32F01_param;
    uint32_t u32F05_param;
    uint32_t u32F07_param;
    uint32_t u32F08_param;
    uint32_t u32F09_param;
    uint32_t u32F1c_param;
} DIGOUTNODE, *PDIGOUTNODE;
AssertNodeSize(DIGOUTNODE, 6);

typedef struct DIGINNODE
{
    CODECCOMMONNODE node;
    uint32_t u32F05_param;
    uint32_t u32F07_param;
    uint32_t u32F08_param;
    uint32_t u32F09_param;
    uint32_t u32F0c_param;
    uint32_t u32F1c_param;
    uint32_t u32F1e_param;
} DIGINNODE, *PDIGINNODE;
AssertNodeSize(DIGINNODE, 7);

typedef struct ADCMUXNODE
{
    CODECCOMMONNODE node;
    uint32_t    u32F01_param;

    uint32_t    u32A_param;
    AMPLIFIER   B_params;
} ADCMUXNODE, *PADCMUXNODE;
AssertNodeSize(ADCMUXNODE, 2 + 60);

typedef struct PCBEEPNODE
{
    CODECCOMMONNODE node;
    uint32_t    u32F07_param;
    uint32_t    u32F0a_param;

    uint32_t    u32A_param;
    AMPLIFIER   B_params;
    uint32_t    u32F1c_param;
} PCBEEPNODE, *PPCBEEPNODE;
AssertNodeSize(PCBEEPNODE, 3 + 60 + 1);

typedef struct CDNODE
{
    CODECCOMMONNODE node;
    uint32_t u32F07_param;
    uint32_t u32F1c_param;
} CDNODE, *PCDNODE;
AssertNodeSize(CDNODE, 2);

typedef struct VOLUMEKNOBNODE
{
    CODECCOMMONNODE node;
    uint32_t    u32F08_param;
    uint32_t    u32F0f_param;
} VOLUMEKNOBNODE, *PVOLUMEKNOBNODE;
AssertNodeSize(VOLUMEKNOBNODE, 2);

typedef struct ADCVOLNODE
{
    CODECCOMMONNODE node;
    uint32_t    u32F0c_param;
    uint32_t    u32F01_param;
    uint32_t    u32A_params;
    AMPLIFIER   B_params;
} ADCVOLNODE, *PADCVOLNODE;
AssertNodeSize(ADCVOLNODE, 3 + 60);

typedef struct RESNODE
{
    CODECCOMMONNODE node;
    uint32_t    u32F05_param;
    uint32_t    u32F06_param;
    uint32_t    u32F07_param;
    uint32_t    u32F1c_param;

    uint32_t    u32A_param;
} RESNODE, *PRESNODE;
AssertNodeSize(RESNODE, 5);

/**
 * Used for the saved state.
 */
typedef struct CODECSAVEDSTATENODE
{
    CODECCOMMONNODE Core;
    uint32_t        au32Params[60 + 6];
} CODECSAVEDSTATENODE;
AssertNodeSize(CODECSAVEDSTATENODE, 60 + 6);

typedef union CODECNODE
{
    CODECCOMMONNODE node;
    ROOTCODECNODE   root;
    AFGCODECNODE    afg;
    DACNODE         dac;
    ADCNODE         adc;
    SPDIFOUTNODE    spdifout;
    SPDIFINNODE     spdifin;
    PORTNODE        port;
    DIGOUTNODE      digout;
    DIGINNODE       digin;
    ADCMUXNODE      adcmux;
    PCBEEPNODE      pcbeep;
    CDNODE          cdnode;
    VOLUMEKNOBNODE  volumeKnob;
    ADCVOLNODE      adcvol;
    RESNODE         reserved;
    CODECSAVEDSTATENODE SavedState;
} CODECNODE, *PCODECNODE;
AssertNodeSize(CODECNODE, 60 + 6);

#define CODEC_NODES_MAX     32

/**
 * HDA codec state (shared).
 */
typedef struct HDACODEC
{
    /** Codec implementation type. */
    CODEC_TYPE enmType;
    /** Codec ID. */
    uint16_t   id;
    uint16_t   u16VendorId;
    uint16_t   u16DeviceId;
    uint8_t    u8BSKU;
    uint8_t    u8AssemblyId;

    CODECNODE  aNodes[CODEC_NODES_MAX];
    uint32_t   cNodes;

    bool       fInReset;
    uint8_t    abPadding1[3]; /**< @todo r=bird: Merge with bPadding2 and eliminate both */

    uint8_t    cTotalNodes;
    uint8_t    u8AdcVolsLineIn;
    uint8_t    u8DacLineOut;
    uint8_t    bPadding2;

    uint8_t    au8Ports[CODEC_NODES_MAX];
    uint8_t    au8Dacs[CODEC_NODES_MAX];
    uint8_t    au8AdcVols[CODEC_NODES_MAX];
    uint8_t    au8Adcs[CODEC_NODES_MAX];
    uint8_t    au8AdcMuxs[CODEC_NODES_MAX];
    uint8_t    au8Pcbeeps[CODEC_NODES_MAX];
    uint8_t    au8SpdifIns[CODEC_NODES_MAX];
    uint8_t    au8SpdifOuts[CODEC_NODES_MAX];
    uint8_t    au8DigInPins[CODEC_NODES_MAX];
    uint8_t    au8DigOutPins[CODEC_NODES_MAX];
    uint8_t    au8Cds[CODEC_NODES_MAX];
    uint8_t    au8VolKnobs[CODEC_NODES_MAX];
    uint8_t    au8Reserveds[CODEC_NODES_MAX];

    STAMCOUNTER StatLookupsR3;
#if 0 /* Codec is not yet kosher enough for ring-0.  @bugref{9890c64} */
    STAMCOUNTER StatLookupsR0;
#endif
} HDACODEC;

/**
 * HDA codec state (ring-0).
 */
typedef struct HDACODECR0
{
    /** @name Public codec functions.
     *  @{  */
#if 0 /** @todo r=bird: why can I just disable these and not get compile errors?  Unfinished code?  No comments.  Not at all amused! */
    DECLR0CALLBACKMEMBER(void, pfnReset, (PHDACODEC pThis, PHDACODECR0 pThisCC));
    DECLR0CALLBACKMEMBER(int,  pfnNodeReset, (PHDACODEC pThis, uint8_t nID, PCODECNODE pNode));
#endif
    DECLR0CALLBACKMEMBER(int,  pfnLookup, (PHDACODEC pThis, PHDACODECR0 pThisCC, uint32_t uVerb, uint64_t *puResp));
    /** @} */
} HDACODECR0;

int hdaR0CodecConstruct(PPDMDEVINS pDevIns, PHDACODEC pThis, PHDACODECR0 pThisCC);

/**
 * HDA codec state (ring-3).
 */
typedef struct HDACODECR3
{
    /** @name Public codec functions.
     *  @{  */
    DECLR3CALLBACKMEMBER(int,  pfnLookup, (PHDACODEC pThis, PHDACODECR3 pThisCC, uint32_t uVerb, uint64_t *puResp));
    DECLR3CALLBACKMEMBER(void, pfnDbgListNodes, (PHDACODEC pThis, PHDACODECR3 pThisCC, PCDBGFINFOHLP pHlp, const char *pszArgs));
    DECLR3CALLBACKMEMBER(void, pfnDbgSelector, (PHDACODEC pThis, PHDACODECR3 pThisCC, PCDBGFINFOHLP pHlp, const char *pszArgs));
    /** @} */

    /** The parent device instance. */
    PPDMDEVINS              pDevIns;

    /** @name Callbacks to the HDA controller, mostly used for multiplexing to the
     *        various host backends.
     * @{ */
    /**
     *
     * Adds a new audio stream to a specific mixer control.
     *
     * Depending on the mixer control the stream then gets assigned to one of the
     * internal mixer sinks, which in turn then handle the mixing of all connected
     * streams to that sink.
     *
     * @return  VBox status code.
     * @param   pDevIns             The device instance.
     * @param   enmMixerCtl         Mixer control to assign new stream to.
     * @param   pCfg                Stream configuration for the new stream.
     */
    DECLR3CALLBACKMEMBER(int,  pfnCbMixerAddStream, (PPDMDEVINS pDevIns, PDMAUDIOMIXERCTL enmMixerCtl, PPDMAUDIOSTREAMCFG pCfg));
    /**
     * Removes a specified mixer control from the HDA's mixer.
     *
     * @return  VBox status code.
     * @param   pDevIns             The device instance.
     * @param   enmMixerCtl         Mixer control to remove.
     */
    DECLR3CALLBACKMEMBER(int,  pfnCbMixerRemoveStream, (PPDMDEVINS pDevIns, PDMAUDIOMIXERCTL enmMixerCtl));
    /**
     * Controls an input / output converter widget, that is, which converter is
     * connected to which stream (and channel).
     *
     * @return  VBox status code.
     * @param   pDevIns             The device instance.
     * @param   enmMixerCtl         Mixer control to set SD stream number and channel for.
     * @param   uSD                 SD stream number (number + 1) to set. Set to 0 for unassign.
     * @param   uChannel            Channel to set. Only valid if a valid SD stream number is specified.
     */
    DECLR3CALLBACKMEMBER(int,  pfnCbMixerControl, (PPDMDEVINS pDevIns, PDMAUDIOMIXERCTL enmMixerCtl, uint8_t uSD, uint8_t uChannel));
    /**
     * Sets the volume of a specified mixer control.
     *
     * @return  IPRT status code.
     * @param   pDevIns             The device instance.
     * @param   enmMixerCtl         Mixer control to set volume for.
     * @param   pVol                Pointer to volume data to set.
     */
    DECLR3CALLBACKMEMBER(int,  pfnCbMixerSetVolume, (PPDMDEVINS pDevIns, PDMAUDIOMIXERCTL enmMixerCtl, PPDMAUDIOVOLUME pVol));
    /** @} */
} HDACODECR3;

int hdaR3CodecConstruct(PPDMDEVINS pDevIns, PHDACODEC pThis, PHDACODECR3 pThisCC, uint16_t uLUN, PCFGMNODE pCfg);
void hdaR3CodecPowerOff(PHDACODECR3 pThisCC);
int hdaR3CodecLoadState(PPDMDEVINS pDevIns, PHDACODEC pThis, PHDACODECR3 pThisCC, PSSMHANDLE pSSM, uint32_t uVersion);
int hdaR3CodecAddStream(PHDACODECR3 pThisCC, PDMAUDIOMIXERCTL enmMixerCtl, PPDMAUDIOSTREAMCFG pCfg);
int hdaR3CodecRemoveStream(PHDACODECR3 pThisCC, PDMAUDIOMIXERCTL enmMixerCtl);

int hdaCodecSaveState(PPDMDEVINS pDevIns, PHDACODEC pThis, PSSMHANDLE pSSM);
void hdaCodecDestruct(PHDACODEC pThis);
void hdaCodecReset(PHDACODEC pThis);

/** @name DevHDA saved state versions
 * @{ */
/** The current staved state version. */
#define HDA_SAVED_STATE_VERSION  HDA_SAVED_STATE_WITHOUT_PERIOD

/** Removed period and redefined wall clock. */
#define HDA_SAVED_STATE_WITHOUT_PERIOD 8
/** Added (Controller):              Current wall clock value (this independent from WALCLK register value).
  * Added (Controller):              Current IRQ level.
  * Added (Per stream):              Ring buffer. This is optional and can be skipped if (not) needed.
  * Added (Per stream):              Struct g_aSSMStreamStateFields7.
  * Added (Per stream):              Struct g_aSSMStreamPeriodFields7.
  * Added (Current BDLE per stream): Struct g_aSSMBDLEDescFields7.
  * Added (Current BDLE per stream): Struct g_aSSMBDLEStateFields7. */
#define HDA_SAVED_STATE_VERSION_7 7
/** Saves the current BDLE state.
 * @since 5.0.14 (r104839) */
#define HDA_SAVED_STATE_VERSION_6 6
/** Introduced dynamic number of streams + stream identifiers for serialization.
 *  Bug: Did not save the BDLE states correctly.
 *  Those will be skipped on load then.
 * @since 5.0.12 (r104520)  */
#define HDA_SAVED_STATE_VERSION_5 5
/** Since this version the number of MMIO registers can be flexible. */
#define HDA_SAVED_STATE_VERSION_4 4
#define HDA_SAVED_STATE_VERSION_3 3
#define HDA_SAVED_STATE_VERSION_2 2
#define HDA_SAVED_STATE_VERSION_1 1
/** @} */

#endif /* !VBOX_INCLUDED_SRC_Audio_DevHdaCodec_h */

