#include "aux.h"
#include "esphome/core/log.h"

namespace esphome {
namespace aux {

static const char *const TAG = "aux.climate";

const uint16_t AUX_HEADER_MARK = 8800;
const uint16_t AUX_HEADER_SPACE = 4580;
const uint16_t AUX_BIT_MARK = 490;
const uint16_t AUX_ONE_SPACE = 1740;
const uint16_t AUX_ZERO_SPACE = 620;

const uint32_t AUX_CARRIER_FREQUENCY = 38000;

const uint8_t AUX_STATE_LENGTH = 13;

const uint8_t AUX_AUTO = 0x00;
const uint8_t AUX_COOL = 0x20;
const uint8_t AUX_DRY = 0x40;
const uint8_t AUX_HEAT = 0x80;
const uint8_t AUX_FAN = 0xc0;

const uint8_t AUX_FAN_AUTO = 0xa0;
const uint8_t AUX_FAN_HIGH = 0x20;
const uint8_t AUX_FAN_MED = 0x40;
const uint8_t AUX_FAN_LOW = 0x60;

const uint8_t AUX_SWING_VER = 0x07;
const uint8_t AUX_SWING_HOR = 0xe0;
const uint8_t AUX_POWER = 0x20;

void AuxClimate::transmit_state() {
  uint8_t remote_state[AUX_STATE_LENGTH] = {0};

  auto temp = (uint8_t) roundf(clamp(this->target_temperature, YKR_K_002E_TEMP_MIN, YKR_K_002E_TEMP_MAX));
  auto swing_ver =
      ((this->swing_mode == climate::CLIMATE_SWING_VERTICAL) || (this->swing_mode == climate::CLIMATE_SWING_BOTH));
  auto swing_hor =
      ((this->swing_mode == climate::CLIMATE_SWING_HORIZONTAL) || (this->swing_mode == climate::CLIMATE_SWING_BOTH));

  remote_state[0] = 0xc3;
  
  remote_state[1] = ((temp - 8) << 3) | (swing_ver ? 0 : AUX_SWING_VER);

  remote_state[2] = swing_hor ? 0 : AUX_SWING_HOR;

  remote_state[9] = (this->mode == climate::CLIMATE_MODE_OFF) ? 0 : AUX_POWER;

  remote_state[11] = 0x08;

  // Fan speed
  switch (this->fan_mode.value()) {
    case climate::CLIMATE_FAN_HIGH:
      remote_state[4] |= AUX_FAN_HIGH;
      break;
    case climate::CLIMATE_FAN_MEDIUM:
      remote_state[4] |= AUX_FAN_MED;
      break;
    case climate::CLIMATE_FAN_LOW:
      remote_state[4] |= AUX_FAN_LOW;
      break;
    case climate::CLIMATE_FAN_AUTO:
      remote_state[4] |= AUX_FAN_AUTO;
      break;
    default:
      break;
  }

  if (this->preset.value() == climate::CLIMATE_PRESET_BOOST) {
    remote_state[5] = 0x40;
  } else {
    remote_state[5] = 0;  // Clear boost bit
  }

  // Mode
  switch (this->mode) {
    case climate::CLIMATE_MODE_AUTO:
      remote_state[6] |= AUX_AUTO;
      break;
    case climate::CLIMATE_MODE_HEAT:
      remote_state[6] |= AUX_HEAT;
      break;
    case climate::CLIMATE_MODE_COOL:
      remote_state[6] |= AUX_COOL;
      break;
    case climate::CLIMATE_MODE_DRY:
      remote_state[6] |= AUX_DRY;
      break;
    case climate::CLIMATE_MODE_FAN_ONLY:
      remote_state[6] |= AUX_FAN;
      break;
    case climate::CLIMATE_MODE_OFF:
      remote_state[6] |= AUX_AUTO;
    default:
      break;
  }

  // Checksum
  for (uint8_t i = 0; i < AUX_STATE_LENGTH - 1; i++)
    remote_state[12] += remote_state[i];

  ESP_LOGV(TAG, "Sending: %02X %02X %02X %02X   %02X %02X %02X %02X   %02X %02X %02X %02X   %02X", remote_state[0],
           remote_state[1], remote_state[2], remote_state[3], remote_state[4], remote_state[5], remote_state[6],
           remote_state[7], remote_state[8], remote_state[9], remote_state[10], remote_state[11], remote_state[12]);

  // Send code
  auto transmit = this->transmitter_->transmit();
  auto *data = transmit.get_data();

  data->set_carrier_frequency(38000);

  // Header
  data->mark(AUX_HEADER_MARK);
  data->space(AUX_HEADER_SPACE);
  // Data
  for (uint8_t i : remote_state) {
    for (uint8_t j = 0; j < 8; j++) {
      data->mark(AUX_BIT_MARK);
      bool bit = i & (1 << j);
      data->space(bit ? AUX_ONE_SPACE : AUX_ZERO_SPACE);
    }
  }
  // Footer
  data->mark(AUX_BIT_MARK);

  transmit.perform();
}

bool AuxClimate::on_receive(remote_base::RemoteReceiveData data) {
  // Validate header
  if (!data.expect_item(AUX_HEADER_MARK, AUX_HEADER_SPACE)) {
    ESP_LOGV(TAG, "Header fail");
    return false;
  }

  uint8_t remote_state[AUX_STATE_LENGTH] = {0};
  // Read all bytes.
  for (int i = 0; i < AUX_STATE_LENGTH; i++) {
    // Read bit
    for (int j = 0; j < 8; j++) {
      if (data.expect_item(AUX_BIT_MARK, AUX_ONE_SPACE)) {
        remote_state[i] |= 1 << j;

      } else if (!data.expect_item(AUX_BIT_MARK, AUX_ZERO_SPACE)) {
        ESP_LOGV(TAG, "Byte %d bit %d fail", i, j);
        return false;
      }
    }

    ESP_LOGVV(TAG, "Byte %d %02X", i, remote_state[i]);
  }
  // Validate footer
  if (!data.expect_mark(AUX_BIT_MARK)) {
    ESP_LOGV(TAG, "Footer fail");
    return false;
  }

  uint8_t checksum = 0;
  // Calculate  checksum and compare with signal value.
  for (uint8_t i = 0; i < AUX_STATE_LENGTH - 1; i++)
    checksum += remote_state[i];

  if (checksum != remote_state[AUX_STATE_LENGTH - 1]) {
    ESP_LOGVV(TAG, "Checksum fail");
    return false;
  }

  ESP_LOGV(TAG, "Received: %02X %02X %02X %02X   %02X %02X %02X %02X   %02X %02X %02X %02X   %02X", remote_state[0],
           remote_state[1], remote_state[2], remote_state[3], remote_state[4], remote_state[5], remote_state[6],
           remote_state[7], remote_state[8], remote_state[9], remote_state[10], remote_state[11], remote_state[12]);

  // verify header remote code
  if (remote_state[0] != 0xc3)
    return false;

  // powr on/off button
  ESP_LOGV(TAG, "Power: %02X", (remote_state[9] & AUX_POWER));

  if ((remote_state[9] & AUX_POWER) != AUX_POWER) {
    this->mode = climate::CLIMATE_MODE_OFF;
  } else {
    auto mode = remote_state[6] & 0xe0;
    ESP_LOGV(TAG, "Mode: %02X", mode);
    switch (mode) {
      case AUX_HEAT:
        this->mode = climate::CLIMATE_MODE_HEAT;
        break;
      case AUX_COOL:
        this->mode = climate::CLIMATE_MODE_COOL;
        break;
      case AUX_DRY:
        this->mode = climate::CLIMATE_MODE_DRY;
        break;
      case AUX_FAN:
        this->mode = climate::CLIMATE_MODE_FAN_ONLY;
        break;
      case AUX_AUTO:
        this->mode = climate::CLIMATE_MODE_AUTO;
        break;
    }
  }

  // Set received temp
  int temp = remote_state[1] & 0xf8;
  ESP_LOGVV(TAG, "Temperature Raw: %02X", temp);
  temp = ((uint8_t) temp >> 3) + 8;
  ESP_LOGVV(TAG, "Temperature Climate: %u", temp);
  this->target_temperature = temp;

  // Set received fan speed
  auto fan = remote_state[4] & 0xe0;
  ESP_LOGVV(TAG, "Fan: %02X", fan);
  switch (fan) {
    case AUX_FAN_HIGH:
      this->fan_mode = climate::CLIMATE_FAN_HIGH;
      break;
    case AUX_FAN_MED:
      this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
      break;
    case AUX_FAN_LOW:
      this->fan_mode = climate::CLIMATE_FAN_LOW;
      break;
    case AUX_FAN_AUTO:
    default:
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
      break;
  }

  // Set received swing status
  ESP_LOGVV(TAG, "Swing status: %02X %02X", remote_state[1] & AUX_SWING_VER, remote_state[2] & AUX_SWING_HOR);
  if (((remote_state[1] & AUX_SWING_VER) != AUX_SWING_VER) &&
      ((remote_state[2] & AUX_SWING_HOR) != AUX_SWING_HOR)) {
    this->swing_mode = climate::CLIMATE_SWING_BOTH;
  } else if ((remote_state[1] & AUX_SWING_VER) != AUX_SWING_VER) {
    this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
  } else if ((remote_state[2] & AUX_SWING_HOR) != AUX_SWING_HOR) {
    this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
  } else {
    this->swing_mode = climate::CLIMATE_SWING_OFF;
  }

  this->publish_state();
  return true;
}

}  // namespace aux
}  // namespace esphome
