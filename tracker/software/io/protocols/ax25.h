/*
    Aerospace Decoder - Copyright (C) 2018 Bob Anderson (VK2GJ)

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*/

/**
 * @file    ax25.h
 * @brief   Definitions for AX25 protocol.
 *
 * @addtogroup protocols
 * @{
 */


#ifndef IO_PROTOCOLS_AX25_H_
#define IO_PROTOCOLS_AX25_H_

/*===========================================================================*/
/* Module local types.                                                       */
/*===========================================================================*/

/**
 * @brief   Type of characters in AX25 packet.
 *
 * @note    Only used in buffers.
 */
typedef uint8_t ax25char_t;
typedef int16_t ax25size_t;

/* AX25 data escape code. */
#define AX25_ESC            0x1BU

/* AX25 definitions for packet data useage. */
#define AX25_MAX_REPEATERS      8
/* Destination & Source. */
#define AX25_MIN_ADDRS          2
/*
 *  Destination & Source + 8 digipeater addresses.
 *  A destination address may specify a generic APRS digipeater path.
 *  In such case the digipeater address fields are overridden by that path.
 */
#define AX25_MAX_ADDRS          10
/* Address positions in frame. */
#define AX25_DESTINATION        0
#define AX25_SOURCE             1
#define AX25_REPEATER_1         2
#define AX25_REPEATER_2         3
#define AX25_REPEATER_3         4
#define AX25_REPEATER_4         5
#define AX25_REPEATER_5         6
#define AX25_REPEATER_6         7
#define AX25_REPEATER_7         8
#define AX25_REPEATER_8         9

/*
 * The maximum address length should be 6 letters, dash, 2 digits and null.
 * Making a total of 10.
 * However, object labels can be 10 characters.
 * So add 2 extra margin bytes.
 */
#define AX25_DS_ADDRESS_LEN     7
#define AX25_MAX_ADDR_LEN       12
#define AX25_CONTROL_LEN        1
#define AX25_PROTOCOL_LEN       1
#define AX25_CRC_LEN            2
#define AX25_FLAG_LEN           1
#define AX25_MIN_INFO_LEN       0


/* An AX.25 packet can have a control byte and no protocol. */
#define AX25_MIN_PACKET_LEN     (AX25_MIN_ADDRS * AX25_DS_ADDRESS_LEN        \
                                  + AX25_CONTROL_LEN)

/*
 * Maximum size for APRS.
 * The payload excluding CRC.
 */
#define AX25_MAX_INFO_LEN       2048

/* An AX.25 packet maximum - closing flag is not included. */
#define AX25_MAX_PACKET_LEN     (AX25_MAX_ADDRS * AX25_DS_ADDRESS_LEN        \
                                  + AX25_CONTROL_LEN                         \
                                  + AX25_PROTOCOL_LEN                        \
                                  + AX25_MAX_INFO_LEN                        \
                                  + AX25_CRC_LEN)

#define AX25_MIN_FRAME          ((AX25_MIN_PACKET_LEN) + AX25_CRC_LEN)
#define AX25_MAX_FRAME          ((AX25_MAX_PACKET_LEN) + AX25_CRC_LEN)

#endif /* IO_PROTOCOLS_AX25_H_ */

/** @} */
