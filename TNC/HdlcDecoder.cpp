// Copyright 2016-2019 Rob Riggs <rob@mobilinkd.com>
// All rights reserved.

#include "HdlcDecoder.hpp"
#include "GPIO.hpp"
#include "Log.h"

namespace mobilinkd { namespace tnc { namespace hdlc {

NewDecoder::optional_result_type NewDecoder::operator()(bool input, bool pll_lock)
{
    optional_result_type result = nullptr;

    auto status = process(input, pll_lock);
    if (status)
    {
        INFO("HDLC decode status = 0x%02x, bits = %d", int(status), int(report_bits));
        if (can_pass(status) and packet->size() > 10)
        {
            result = packet;
            packet = nullptr;
        } else {
            packet->clear();
        }
    }

    return result;
}

uint8_t NewDecoder::process(bool input, bool pll_lock)
{
    uint8_t result_code = 0;

    if (state == State::IDLE and not pll_lock) return result_code;

    while (packet == nullptr) {
        packet = ioFramePool().acquire();
        if (!packet) osThreadYield();
    }

    if (pll_lock) {
        if (ones == 5) {
            if (input) {
                // flag byte
                flag = 1;
            } else {
                // bit stuffing...
                flag = 0;
                ones = 0;
                return result_code;
            }
        }

        buffer >>= 1;
        buffer |= (input * 128);
        bits += 1;                      // Free-running until Sync byte.
        if (input) {
            ++ones;
        } else {
            ones = 0;
        }

        if (flag) {
            switch (buffer) {
            case 0x7E:
                if (packet->size() > 0) {
                    // We have started decoding a packet.
                    packet->parse_fcs();
                    report_bits = bits;
                    if (packet->ok()) {
                        // CRC is OK, assume a good packet, even if there are
                        // extraneous bits decoded.
                        result_code = STATUS_OK;
                    } else if (bits == 8) {
                        // Otherwise, if there is a CRC error but we are on
                        // an even byte boundary, flag a CRC error.  This is
                        // used by the "pass all" rule.
                        result_code = STATUS_CRC_ERROR;
                    } else {
                        // Extraneous bits mean we have a framing error.
                        // We should not pass this frame up the stack.
                        result_code = STATUS_FRAME_ERROR;
                    }
                }
                state = State::SYNC;
                flag = 0;
                bits = 0;
                break;
            case 0xFE:
                if (packet->size()) {
                    result_code = STATUS_FRAME_ABORT;
                }
                state = State::IDLE;
                flag = 0;
                bits = 0;
                break;
            default:
                /* pass */
                break;
            }
            return result_code;
        }

        switch (state)
        {

        case State::IDLE:
            break;

        case State::SYNC:
            if (bits == 8) {    // 8th bit.
                // Start of frame data.
                state = State::RECEIVE;
                packet->push_back(buffer);
                bits = 0;
            }
            break;

        case State::RECEIVE:
            if (bits == 8) {    // 8th bit.
                packet->push_back(buffer);
                bits = 0;
            }
        }
    } else {
        // PLL unlocked.
        // Note the rules here are the same as above.
        report_bits = bits;
        if (packet->size() > 2)
        {
            packet->parse_fcs();
            if (packet->ok())
            {
                result_code = STATUS_OK;
            }
            else if (bits == 8)
            {
                result_code = STATUS_CRC_ERROR;
            }
            else
            {
                result_code = STATUS_NO_CARRIER;
            }
        }
        else
        {
            result_code = STATUS_NO_CARRIER;
        }

        if (state != State::IDLE) {
            buffer = 0;
            flag = 0;
            bits = 0;
            state = State::IDLE;
        }
    }

    return result_code;
}

}}} // mobilinkd::tnc::hdlc
