#pragma once
#include "tas5805m.h"

// From 2024-04-15

/* snippet for Treble Ctrl EcoSpk TAS5805M I2C 7-Bit Addr = 0x2c */

/* first ********************************************
 * set Book
 * and Page
 ****************************************************/
const tas5805m_cfg_reg_t tas5805m_treble_preconfig[] = {
    { 0x00, 0x00 },     // Page 0
    { 0x7f, 0xAA },     // Book 0xAA
    { 0x00, 0x26 }      // Page 26
};

/* second: *******************************************
 * set Biquad Coeffs. 
 * (BQ15 Reg. 0x40 .. 0x44 .. 0x48 .. 0x4C .. 0x50)
 *****************************************************/
  
// Treble +6dB
const tas5805m_cfg_reg_t tas5805m_treble_plus_6db_config[] = {
    { CFG_META_BURST, 21 },
    { 0x40, 0x0d },
    { 0xca, 0x1e }, 
    { 0x34, 0xee }, 
    { 0x91, 0x90 }, 
    { 0x51, 0x05 }, 
    { 0x82, 0x3e }, 
    { 0xf2, 0x08 }, 
    { 0x45, 0x51 },
    { 0x61, 0xfd },
    { 0xdc, 0xc1 },
    { 0x28, 0x00 } 
};

// Treble +5dB
const tas5805m_cfg_reg_t tas5805m_treble_plus_5db_config[] = {
    { CFG_META_BURST, 21 },
    { 0x40, 0x0c }, 
    { 0x98, 0x27 }, 
    { 0x70, 0xf0 }, 
    { 0x4c, 0x91 }, 
    { 0x6e, 0x04 }, 
    { 0xe4, 0xb8 }, 
    { 0x00, 0x08 }, 
    { 0x70, 0x30 }, 
    { 0xf1, 0xfd },
    { 0xc6, 0x5e }, 
    { 0x31, 0x00 }
};    

// Treble +4dB
const tas5805m_cfg_reg_t tas5805m_treble_plus_4db_config[] = {
    { CFG_META_BURST, 21 },
    { 0x40, 0x0b }, 
    { 0x80, 0x92 }, 
    { 0xfa, 0xf1 }, 
    { 0xdd, 0x65 }, 
    { 0xbd, 0x04 }, 
    { 0x57, 0xba }, 
    { 0xdc, 0x08 }, 
    { 0x9a, 0x6b }, 
    { 0x2b, 0xfd }, 
    { 0xaf, 0xe1 }, 
    { 0x42, 0x00 } 
};    

// Treble +3dB
const tas5805m_cfg_reg_t tas5805m_treble_plus_3db_config[] = {
    { CFG_META_BURST, 21 },
    { 0x40, 0x0a }, 
    { 0x81, 0x23 }, 
    { 0xa7, 0xf3 }, 
    { 0x47, 0xe8 }, 
    { 0x07, 0x03 }, 
    { 0xd9, 0xa5 }, 
    { 0x19, 0x08 }, 
    { 0xc3, 0xff }, 
    { 0x62, 0xfd }, 
    { 0x99, 0x4f }, 
    { 0xd6, 0x00 }
};

// Treble +2dB
const tas5805m_cfg_reg_t tas5805m_treble_plus_2db_config[] = {
    { CFG_META_BURST, 21 },
    { 0x40, 0x09 }, 
    { 0x97, 0xcb }, 
    { 0x94, 0xf4 }, 
    { 0x8f, 0x9c }, 
    { 0xec, 0x03 }, 
    { 0x68, 0xfb }, 
    { 0x24, 0x08 }, 
    { 0xec, 0xed }, 
    { 0x16, 0xfd }, 
    { 0x82, 0xaf }, 
    { 0x45, 0x00 }
};

// Treble +1dB
const tas5805m_cfg_reg_t tas5805m_treble_plus_1db_config[] = {
    { CFG_META_BURST, 21 },
    { 0x40, 0x08}, 
    { 0xc2, 0xa8}, 
    { 0x71, 0xf5}, 
    { 0xb7, 0xba}, 
    { 0x00, 0x03}, 
    { 0x04, 0x64}, 
    { 0xe6, 0x09}, 
    { 0x15, 0x33}, 
    { 0xe8, 0xfd}, 
    { 0x6c, 0x04}, 
    { 0xc1, 0x00}
};

// Treble 0dB
const tas5805m_cfg_reg_t tas5805m_treble_0db_config[] = {
    { CFG_META_BURST, 21 },
    { 0x40, 0x08 }, 
    { 0x00, 0x00 }, 
    { 0x00, 0xf6 }, 
    { 0xc3, 0x2c }, 
    { 0x5d, 0x02 }, 
    { 0xaa, 0xaa }, 
    { 0xab, 0x09 }, 
    { 0x3c, 0xd3 }, 
    { 0xa3, 0xfd }, 
    { 0x55, 0x55 }, 
    { 0x55, 0x00 }
};

// Treble -1dB
const tas5805m_cfg_reg_t tas5805m_treble_minus_1db_config[] = {
    { CFG_META_BURST, 21 },
    { 0x40, 0x07 }, 
    { 0x4e, 0x3c }, 
    { 0xe9, 0xf7 }, 
    { 0xb4, 0x9e }, 
    { 0xb4, 0x02 }, 
    { 0x5a, 0xb2 }, 
    { 0x4a, 0x09 }, 
    { 0x63, 0xcc }, 
    { 0x34, 0xfd }, 
    { 0x3e, 0xa5 }, 
    { 0xe6, 0x00 }
};

// Treble -2dB
const tas5805m_cfg_reg_t tas5805m_treble_minus_2db_config[] = {
    { CFG_META_BURST, 21 },
    { 0x40, 0x06 }, 
    { 0xab, 0xeb }, 
    { 0xc0, 0xf8 }, 
    { 0x8e, 0x7e }, 
    { 0xde, 0x02 }, 
    { 0x13, 0x7c }, 
    { 0x87, 0x09 }, 
    { 0x8a, 0x1d }, 
    { 0xac, 0xfd }, 
    { 0x27, 0xfb }, 
    { 0x2f, 0x00 }
};

// Treble -3dB
const tas5805m_cfg_reg_t tas5805m_treble_minus_3db_config[] = {
    { CFG_META_BURST, 21 },
    { 0x40, 0x06 },  
    { 0x17, 0xb8 },  
    { 0x4b, 0xf9 },  
    { 0x53, 0x03 },  
    { 0x0a, 0x01 },  
    { 0xd4, 0x22 },  
    { 0xaa, 0x09 },  
    { 0xaf, 0xc8 },  
    { 0x41, 0xfd },  
    { 0x11, 0x59 },  
    { 0xc0, 0x00 }
 };

// Treble -4
const tas5805m_cfg_reg_t tas5805m_treble_minus_4db_config[] = {
    { CFG_META_BURST, 21 },
        { 0x40, 0x05 }, 
        { 0x90, 0x6a }, 
        { 0xf9, 0xfa }, 
        { 0x04, 0x2e }, 
        { 0x72, 0x01 }, 
        { 0x9b, 0xd4 }, 
        { 0x4e, 0x09 }, 
        { 0xd4, 0xcc }, 
        { 0x45, 0xfc }, 
        { 0xfa, 0xc6 }, 
        { 0x01, 0x00 } 
};

// Treble -5
const tas5805m_cfg_reg_t tas5805m_treble_minus_5db_config[] = {
    { CFG_META_BURST, 21 },
    { 0x40, 0x05 }, 
    { 0x14, 0xe6 }, 
    { 0x8f, 0xfa }, 
    { 0xa3, 0xd5 }, 
    { 0xbf, 0x01 }, 
    { 0x69, 0xd5 }, 
    { 0x57, 0x09 }, 
    { 0xf9, 0x2a }, 
    { 0x2e, 0xfc }, 
    { 0xe4, 0x44 },
    { 0x2d, 0x00 }  
};

// Treble -6
const tas5805m_cfg_reg_t tas5805m_treble_minus_6db_config[] = {
    { CFG_META_BURST, 21 },
    { 0x40, 0x04 }, 
    { 0xa4, 0x25 }, 
    { 0xfd, 0xfb }, 
    { 0x33, 0xa3 }, 
    { 0x05, 0x01 }, 
    { 0x3d, 0x7c }, 
    { 0x1b, 0x0a }, 
    { 0x1c, 0xe2 }, 
    { 0x8f, 0xfc }, 
    { 0xcd, 0xd8 }, 
    { 0x54, 0x00 } 
};

#define TAS5805M_TREBLE_PRECONFIG_SIZE (sizeof(tas5805m_treble_preconfig) / sizeof(tas5805m_treble_preconfig[0]))

// The bass configs are all the same size
#define TAS5805M_TREBLE_CONFIG_SIZE (sizeof(tas5805m_treble_plus_6db_config) / sizeof(tas5805m_treble_plus_6db_config[0]))
