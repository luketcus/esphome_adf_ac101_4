#include "adf_i2s_out.h"
#ifdef USE_ESP_IDF

#include "../../adf_pipeline/sdk_ext.h"
#include "i2s_stream_mod.h"
#include "../../adf_pipeline/adf_pipeline.h"
#include "../external_dac.h"

namespace esphome {
using namespace esp_adf;
namespace i2s_audio {

static const char *const TAG = "adf_i2s_out";


void ADFElementI2SOut::setup() {
  this->supported_bits_per_sample_.push_back(16);
  this->supported_samples_rates_.push_back(16000);
  this->supported_samples_rates_.push_back(44100);
  this->sample_rate_ = 16000;
  this->bits_per_sample_ = 16;
  this->set_external_dac_channels(2);
  this->channels_ = 2;
}

bool ADFElementI2SOut::init_adf_elements_() {
  if (this->sdk_audio_elements_.size() > 0)
    return true;

  if (this->external_dac_ != nullptr){
    this->external_dac_->init_device();
  }

i2s_driver_config_t i2s_config;
if( this->parent_->adjustable()){
  i2s_config  = {
        .mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_IRAM,
        .dma_buf_count = 4,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
        .mclk_multiple = I2S_MCLK_MULTIPLE_DEFAULT,
        .bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT,
  #if SOC_I2S_SUPPORTS_TDM
        .chan_mask = I2S_CHANNEL_MONO,
        .total_chan = 0,
        .left_align = false,
        .big_edin = false,
        .bit_order_msb = false,
        .skip_msk = false,
  #endif
  };
} else {
  i2s_config = this->parent_->get_i2s_cfg();
}

  i2s_stream_cfg_t i2s_cfg = {
      .type = AUDIO_STREAM_WRITER,
      .i2s_config = i2s_config,
      .i2s_port = this->parent_->get_port(),
      .use_alc = this->use_adf_alc_,
      .volume = 0,
      .out_rb_size = (6 * 1024),
      .task_stack = I2S_STREAM_TASK_STACK,
      .task_core = I2S_STREAM_TASK_CORE,
      .task_prio = I2S_STREAM_TASK_PRIO,
      .stack_in_ext = false,
      .multi_out_num = 0,
      .uninstall_drv = false,
      .need_expand = false,
      .expand_src_bits = I2S_BITS_PER_SAMPLE_16BIT,
  };

  this->adf_i2s_stream_writer_ = i2s_stream_init(&i2s_cfg);
  this->adf_i2s_stream_writer_->buf_size = 1 * 1024;

  this->install_i2s_driver(i2s_config);
  i2s_zero_dma_buffer(this->parent_->get_port());
  /*
  if (i2s_stream_set_clk(this->adf_i2s_stream_writer_, 48000, 16,
                           2) != ESP_OK) {
      esph_log_e(TAG, "error while setting sample rate and bit depth,");
  }
  */
  if (this->external_dac_ != nullptr){
    this->external_dac_->apply_i2s_settings(i2s_config);
  }

  sdk_audio_elements_.push_back(this->adf_i2s_stream_writer_);
  sdk_element_tags_.push_back("i2s_out");

  this->bits_per_sample_ = 16;
  this->sample_rate_ = 16000;
  this->channels_ = 2;

  return true;
}

void ADFElementI2SOut::clear_adf_elements_(){
  this->sdk_audio_elements_.clear();
  this->sdk_element_tags_.clear();
  this->release_i2s_access();
}

bool ADFElementI2SOut::is_ready(){
  return this->set_i2s_access();
}

void ADFElementI2SOut::on_settings_request(AudioPipelineSettingsRequest &request) {
  if ( !this->adf_i2s_stream_writer_ ){
    return;
  }

  if (this->parent_->adjustable()){
    bool rate_bits_channels_updated = false;
    if (request.sampling_rate > 0 && (uint32_t) request.sampling_rate != this->sample_rate_) {
      this->sample_rate_ = request.sampling_rate;
      rate_bits_channels_updated = true;
    }

    if(request.number_of_channels > 0 && (uint8_t) request.number_of_channels != this->channels_)
    {
      this->channels_ = request.number_of_channels;
      rate_bits_channels_updated = true;
    }

    if (request.bit_depth > 0 && (uint8_t) request.bit_depth != this->bits_per_sample_) {
      bool supported = false;
      for (auto supported_bits : this->supported_bits_per_sample_) {
        if (supported_bits == (uint8_t) request.bit_depth) {
          supported = true;
        }
      }
      if (!supported) {
        request.failed = true;
        request.failed_by = this;
        return;
      }
      this->bits_per_sample_ = request.bit_depth;
      rate_bits_channels_updated = true;
    }

    if (rate_bits_channels_updated) {

      audio_element_set_music_info(this->adf_i2s_stream_writer_,this->sample_rate_, this->channels_, this->bits_per_sample_ );

      if (i2s_stream_set_clk(this->adf_i2s_stream_writer_, this->sample_rate_, this->bits_per_sample_,
                            this->channels_) != ESP_OK) {
        esph_log_e(TAG, "error while setting sample rate and bit depth,");
        request.failed = true;
        request.failed_by = this;
        return;
      }
    }
  }
  // final pipeline settings are unset
  if (request.final_sampling_rate == -1) {
    esph_log_d(TAG, "Set final i2s settings: %d", this->sample_rate_);
    request.final_sampling_rate = this->sample_rate_;
    request.final_bit_depth = this->bits_per_sample_;
    request.final_number_of_channels = this->channels_;
  } else if (
       request.final_sampling_rate != this->sample_rate_
    || request.final_bit_depth != this->bits_per_sample_
    || request.final_number_of_channels != this->channels_
  )
  {
    request.failed = true;
    request.failed_by = this;
  }

  if (this->use_adf_alc_ && request.target_volume > -1) {
    int target_volume = (int) (request.target_volume * 64.) - 32;
    if (i2s_alc_volume_set(this->adf_i2s_stream_writer_, target_volume) != ESP_OK) {
      esph_log_e(TAG, "error setting volume to %d", target_volume);
      request.failed = true;
      request.failed_by = this;
      return;
    }
  }

  if( this->external_dac_ != nullptr && request.target_volume > -1){
    this->external_dac_->set_volume( request.target_volume);
  }

}

}  // namespace i2s_audio
}  // namespace esphome
#endif
