//
//  9918.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 25/11/2017.
//  Copyright © 2017 Thomas Harte. All rights reserved.
//

#include "9918.hpp"

#include <cassert>
#include <cstring>

using namespace TI;

namespace {

const uint32_t palette_pack(uint8_t r, uint8_t g, uint8_t b) {
	uint32_t result = 0;
	uint8_t *const result_ptr = reinterpret_cast<uint8_t *>(&result);
	result_ptr[0] = r;
	result_ptr[1] = g;
	result_ptr[2] = b;
	result_ptr[3] = 0;
	return result;
}

const uint32_t palette[16] = {
	palette_pack(0, 0, 0),
	palette_pack(0, 0, 0),
	palette_pack(33, 200, 66),
	palette_pack(94, 220, 120),

	palette_pack(84, 85, 237),
	palette_pack(125, 118, 252),
	palette_pack(212, 82, 77),
	palette_pack(66, 235, 245),

	palette_pack(252, 85, 84),
	palette_pack(255, 121, 120),
	palette_pack(212, 193, 84),
	palette_pack(230, 206, 128),

	palette_pack(33, 176, 59),
	palette_pack(201, 91, 186),
	palette_pack(204, 204, 204),
	palette_pack(255, 255, 255)
};

const uint8_t StatusInterrupt = 0x80;
const uint8_t StatusFifthSprite = 0x40;

const int StatusSpriteCollisionShift = 5;
const uint8_t StatusSpriteCollision = 0x20;

}

TMS9918::TMS9918(Personality p) :
	// 342 internal cycles are 228/227.5ths of a line, so 341.25 cycles should be a whole
	// line. Therefore multiply everything by four, but set line length to 1365 rather than 342*4 = 1368.
	crt_(new Outputs::CRT::CRT(1365, 4, Outputs::CRT::DisplayType::NTSC60, 4)) {
	crt_->set_rgb_sampling_function(
		"vec3 rgb_sample(usampler2D sampler, vec2 coordinate, vec2 icoordinate)"
		"{"
			"return texture(sampler, coordinate).rgb / vec3(255.0);"
		"}");
	crt_->set_output_device(Outputs::CRT::OutputDevice::Monitor);
	crt_->set_visible_area(Outputs::CRT::Rect(0.055f, 0.025f, 0.9f, 0.9f));
}

std::shared_ptr<Outputs::CRT::CRT> TMS9918::get_crt() {
	return crt_;
}

void TMS9918::test_sprite(int sprite_number) {
	if(!(status_ & StatusFifthSprite)) {
		status_ = static_cast<uint8_t>((status_ & ~31) | sprite_number);
	}
	if(sprites_stopped_)
		return;

	const int sprite_position = ram_[sprite_attribute_table_address_ + (sprite_number << 2)];
	// A sprite Y of 208 means "don't scan the list any further".
	if(sprite_position == 208) {
		sprites_stopped_ = true;
		return;
	}

	const int sprite_row = (row_ - sprite_position)&255;
	if(sprite_row < 0 || sprite_row >= sprite_height_) return;

	const int active_sprite_slot = sprite_sets_[active_sprite_set_].active_sprite_slot;
	if(active_sprite_slot == 4) {
		status_ |= StatusFifthSprite;
		return;
	}

	SpriteSet::ActiveSprite &sprite = sprite_sets_[active_sprite_set_].active_sprites[active_sprite_slot];
	sprite.index = sprite_number;
	sprite.row = sprite_row >> (sprites_magnified_ ? 1 : 0);
	sprite_sets_[active_sprite_set_].active_sprite_slot++;
}

void TMS9918::get_sprite_contents(int field, int cycles_left, int screen_row) {
	int sprite_id = field / 6;
	field %= 6;

	while(true) {
		const int cycles_in_sprite = std::min(cycles_left, 6 - field);
		cycles_left -= cycles_in_sprite;
		const int final_field = cycles_in_sprite + field;

		assert(sprite_id < 4);
		SpriteSet::ActiveSprite &sprite = sprite_sets_[active_sprite_set_].active_sprites[sprite_id];

		if(field < 4) {
			std::memcpy(
				&sprite.info[field],
				&ram_[sprite_attribute_table_address_ + (sprite.index << 2) + field],
				static_cast<size_t>(std::min(4, final_field) - field));
		}

		field = std::min(4, final_field);
		const int sprite_offset = sprite.info[2] & ~(sprites_16x16_ ? 3 : 0);
		const int sprite_address = sprite_generator_table_address_ + (sprite_offset << 3) + sprite.row;
		while(field < final_field) {
			sprite.image[field - 4] = ram_[sprite_address + ((field - 4) << 4)];
			field++;
		}

		if(!cycles_left) return;
		field = 0;
		sprite_id++;
	}
}

void TMS9918::run_for(const HalfCycles cycles) {
	// As specific as I've been able to get:
	// Scanline time is always 228 cycles.
	// PAL output is 313 lines total. NTSC output is 262 lines total.
	// Interrupt is signalled upon entering the lower border.

	// Keep a count of cycles separate from internal counts to avoid
	// potential errors mapping back and forth.
	half_cycles_into_frame_ = (half_cycles_into_frame_ + cycles) % HalfCycles(frame_lines_ * 228 * 2);

	// Convert to 342 cycles per line; the internal clock is 1.5 times the
	// nominal 3.579545 Mhz that I've advertised for this part.
	int int_cycles = (cycles.as_int() * 3) + cycles_error_;
	cycles_error_ = int_cycles & 7;
	int_cycles >>= 3;
	if(!int_cycles) return;

	while(int_cycles) {
		// Determine how much time has passed in the remainder of this line, and proceed.
		int cycles_left = std::min(342 - column_, int_cycles);
		column_ += cycles_left;		// column_ is now the column that has been reached in this line.
		int_cycles -= cycles_left;	// Count down duration to run for.



		// ------------------------------
		// TODO: memory access slot here.
		// ------------------------------



		// ------------------------------
		// Perform video memory accesses.
		// ------------------------------
		if(row_	< 192 && !blank_screen_) {
			const int access_slot = column_ >> 1;	// There are only 171 available memory accesses per line.
			switch(line_mode_) {
				case LineMode::Text:
					access_pointer_ = std::min(30, access_slot);
					if(access_pointer_ >= 30 && access_pointer_ < 150) {
						const int row_base = pattern_name_address_ + (row_ >> 3) * 40;
						const int end = std::min(150, access_slot);

						// Pattern names are collected every third window starting from window 30.
						const int pattern_names_start = (access_pointer_ - 30 + 2) / 3;
						const int pattern_names_end = (end - 30 + 2) / 3;
						std::memcpy(&pattern_names_[pattern_names_start], &ram_[row_base + pattern_names_start], static_cast<size_t>(pattern_names_end - pattern_names_start));

						// Patterns are collected every third window starting from window 32.
						const int pattern_buffer_start = (access_pointer_ - 32 + 2) / 3;
						const int pattern_buffer_end = (end - 32 + 2) / 3;
						for(int column = pattern_buffer_start; column < pattern_buffer_end; ++column) {
							pattern_buffer_[column] = ram_[pattern_generator_table_address_ + (pattern_names_[column] << 3) + (row_ & 7)];
						}
					}
				break;

				case LineMode::Character:
					// Four access windows: no collection.
					if(access_pointer_ < 5)
						access_pointer_ = std::min(5, access_slot);

					// Then ten access windows are filled with collection of sprite 3 and 4 details.
					if(access_pointer_ >= 5 && access_pointer_ < 15) {
						int end = std::min(15, access_slot);
						get_sprite_contents(access_pointer_ - 5 + 14, end - access_pointer_, row_ - 1);
						access_pointer_ = std::min(15, access_slot);
					}

					// Four more access windows: no collection.
					if(access_pointer_ >= 15 && access_pointer_ < 19)
						access_pointer_ = std::min(19, access_slot);

					// Start new sprite set if this is location 19.
					if(access_pointer_ == 19) {
						active_sprite_set_ ^= 1;
						sprite_sets_[active_sprite_set_].active_sprite_slot = 0;
						sprites_stopped_ = false;
					}

					// Then eight access windows fetch the y position for the first eight sprites.
					while(access_pointer_ < 27 && access_pointer_ < access_slot) {
						test_sprite(access_pointer_ - 19);
						access_pointer_++;
					}

					// The next 128 access slots are video and sprite collection interleaved.
					if(access_pointer_ >= 27 && access_pointer_ < 155) {
						int end = std::min(155, access_slot);

						int row_base = pattern_name_address_;
						int pattern_base = pattern_generator_table_address_;
						int colour_base = colour_table_address_;
						if(screen_mode_ == 1) {
							pattern_base &= 0x2000 | ((row_ & 0xc0) << 5);
							colour_base &= 0x2000 | ((row_ & 0xc0) << 5);
						}
						row_base += (row_ << 2)&~31;

						// Pattern names are collected every fourth window starting from window 27.
						const int pattern_names_start = (access_pointer_ - 27 + 3) >> 2;
						const int pattern_names_end = (end - 27 + 3) >> 2;
						std::memcpy(&pattern_names_[pattern_names_start], &ram_[row_base + pattern_names_start], static_cast<size_t>(pattern_names_end - pattern_names_start));

						// Colours are collected ever fourth window starting from window 29.
						const int colours_start = (access_pointer_ - 29 + 3) >> 2;
						const int colours_end = (end - 29 + 3) >> 2;
						if(screen_mode_ != 1) {
							for(int column = colours_start; column < colours_end; ++column) {
								colour_buffer_[column] = ram_[colour_base + (pattern_names_[column] >> 3)];
							}
						} else {
							for(int column = colours_start; column < colours_end; ++column) {
								colour_buffer_[column] = ram_[colour_base + (pattern_names_[column] << 3) + (row_ & 7)];
							}
						}

						// Patterns are collected ever fourth window starting from window 30.
						const int pattern_buffer_start = (access_pointer_ - 30 + 3) >> 2;
						const int pattern_buffer_end = (end - 30 + 3) >> 2;
						for(int column = pattern_buffer_start; column < pattern_buffer_end; ++column) {
							pattern_buffer_[column] = ram_[pattern_base + (pattern_names_[column] << 3) + (row_ & 7)];
						}

						// Sprite slots occur in three quarters of ever fourth window starting from window 28.
						const int sprite_start = (access_pointer_ - 28 + 3) >> 2;
						const int sprite_end = (end - 28 + 3) >> 2;
						for(int column = sprite_start; column < sprite_end; ++column) {
							if(column&3) {
								test_sprite(7 + column - (column >> 2));
							}
						}

						access_pointer_ = std::min(155, access_slot);
					}

					// Two access windows: no collection.
					if(access_pointer_ < 157)
						access_pointer_ = std::min(157, access_slot);

					// Fourteen access windows: collect initial sprite information.
					if(access_pointer_ >= 157 && access_pointer_ < 171) {
						int end = std::min(171, access_slot);
						get_sprite_contents(access_pointer_ - 157, end - access_pointer_, row_);
						access_pointer_ = std::min(171, access_slot);
					}
				break;
			}
		}
		// --------------------------
		// End video memory accesses.
		// --------------------------



		// --------------------
		// Output video stream.
		// --------------------
		if(row_	< 192 && !blank_screen_) {
			// ----------------------
			// Output horizontal sync
			// ----------------------
			if(!output_column_ && column_ >= 26) {
				crt_->output_sync(13 * 4);
				crt_->output_default_colour_burst(13 * 4);
				output_column_ = 26;
			}

			// -------------------
			// Output left border.
			// -------------------
			if(output_column_ >= 26) {
				int pixels_end = std::min(first_pixel_column_, column_);
				if(output_column_ < pixels_end) {
					output_border(pixels_end - output_column_);
					output_column_ = pixels_end;

					// Grab a pointer for drawing pixels to, if the moment has arrived.
					if(pixels_end == first_pixel_column_) {
						pixel_base_ = pixel_target_ = reinterpret_cast<uint32_t *>(crt_->allocate_write_area(static_cast<unsigned int>(first_right_border_column_ - first_pixel_column_)));
					}
				}
			}

			// --------------
			// Output pixels.
			// --------------
			if(output_column_ >= first_pixel_column_) {
				int pixels_end = std::min(first_right_border_column_, column_);

				if(output_column_ < pixels_end) {
					switch(line_mode_) {
						case LineMode::Text: {
							const uint32_t colours[2] = { palette[background_colour_], palette[text_colour_] };

							const int shift = (output_column_ - first_pixel_column_) % 6;
							int byte_column = (output_column_ - first_pixel_column_) / 6;
							int pattern = pattern_buffer_[byte_column] << shift;
							int pixels_left = pixels_end - output_column_;
							int length = std::min(pixels_left, 6 - shift);
							while(true) {
								pixels_left -= length;
								while(length--) {
									*pixel_target_ = colours[(pattern >> 7)&0x01];
									pixel_target_++;
									pattern <<= 1;
								}

								if(!pixels_left) break;
								length = std::min(6, pixels_left);
								byte_column++;
								pattern = pattern_buffer_[byte_column];
							}
							output_column_ = pixels_end;
						} break;

						case LineMode::Character: {
							// If this is the start of the visible area, seed sprite shifter positions.
							SpriteSet &sprite_set = sprite_sets_[active_sprite_set_ ^ 1];
							if(line_mode_ == LineMode::Character && output_column_ == first_pixel_column_) {
								int c = sprite_set.active_sprite_slot;
								while(c--) {
									SpriteSet::ActiveSprite &sprite = sprite_set.active_sprites[c];
									sprite.shift_position = -sprite.info[1];
									if(sprite.info[3] & 0x80) {
										sprite.shift_position += 32;
										if(sprite.shift_position > 0 && !sprites_magnified_)
											sprite.shift_position *= 2;
									}
								}
							}

							// Paint the background tiles.
							const int shift = (output_column_ - first_pixel_column_) & 7;
							int byte_column = (output_column_ - first_pixel_column_) >> 3;

							const int pixels_left = pixels_end - output_column_;
							int length = std::min(pixels_left, 8 - shift);

							int pattern = pattern_buffer_[byte_column] << shift;
							uint8_t colour = colour_buffer_[byte_column];
							uint32_t colours[2] = {
								palette[(colour & 15) ? (colour & 15) : background_colour_],
								palette[(colour >> 4) ? (colour >> 4) : background_colour_]
							};

							int background_pixels_left = pixels_left;
							while(true) {
								background_pixels_left -= length;
								while(length--) {
									*pixel_target_ = colours[(pattern >> 7)&0x01];
									pixel_target_++;
									pattern <<= 1;
								}

								if(!background_pixels_left) break;
								length = std::min(8, background_pixels_left);
								byte_column++;

								pattern = pattern_buffer_[byte_column];
								colour = colour_buffer_[byte_column];
								colours[0] = palette[(colour & 15) ? (colour & 15) : background_colour_];
								colours[1] = palette[(colour >> 4) ? (colour >> 4) : background_colour_];
							}

							// Paint sprites and check for collisions.
							if(sprite_set.active_sprite_slot) {
								int sprite_pixels_left = pixels_left;
								const int shift_advance = sprites_magnified_ ? 1 : 2;
//								const uint32_t sprite_colour_selection_masks[2] = {0x00000000, 0xffffffff};
								while(sprite_pixels_left--) {
									uint32_t sprite_colour = pixel_base_[output_column_ - first_pixel_column_];
									int sprite_mask = 0;
									int c = sprite_set.active_sprite_slot;
									while(c--) {
										SpriteSet::ActiveSprite &sprite = sprite_set.active_sprites[c];

										if(sprite.shift_position < 0) {
											sprite.shift_position++;
											continue;
										} else if(sprite.shift_position < 32) {
											int mask = sprite.image[sprite.shift_position >> 4] << ((sprite.shift_position&15) >> 1);
											mask = (mask >> 7) & 1;
											status_ |= (mask & sprite_mask) << StatusSpriteCollisionShift;
											sprite_mask |= mask;
											sprite.shift_position += shift_advance;

											// TODO: can a non-conditional version be found like that commented out below, but
											// which accounts for colour 0 being invisible?
//											sprite_colour = (sprite_colour & sprite_colour_selection_masks[mask^1]) | (palette[sprite.info[3]&15] & sprite_colour_selection_masks[mask]);
											if((sprite.info[3]&15) && mask) {
												sprite_colour = palette[sprite.info[3]&15];
											}
										}
									}

									pixel_base_[output_column_ - first_pixel_column_] = sprite_colour;
									output_column_++;
								}
							}

							output_column_ = pixels_end;
						} break;
					}

					if(output_column_ == first_right_border_column_) {
						crt_->output_data(static_cast<unsigned int>(first_right_border_column_ - first_pixel_column_) * 4, 4);
						pixel_target_ = nullptr;
					}
				}
			}

			// --------------------
			// Output right border.
			// --------------------
			if(output_column_ >= first_right_border_column_) {
				output_border(column_ - output_column_);
				output_column_ = column_;
			}
		} else if(row_ >= first_vsync_line_ && row_ < first_vsync_line_+3) {
			// Vertical sync.
			if(column_ == 342) {
				crt_->output_sync(342 * 4);
			}
		} else {
			// Blank.
			if(!output_column_ && column_ >= 26) {
				crt_->output_sync(13 * 4);
				crt_->output_default_colour_burst(13 * 4);
				output_column_ = 26;
			}
			if(output_column_ >= 26) {
				output_border(column_ - output_column_);
				output_column_ = column_;
			}
		}
		// -----------------
		// End video stream.
		// -----------------



		// -----------------------------------
		// Prepare for next line, potentially.
		// -----------------------------------
		if(column_ == 342) {
			access_pointer_ = column_ = output_column_ = 0;
			row_ = (row_ + 1) % frame_lines_;
			if(row_ == 192) status_ |= StatusInterrupt;

			screen_mode_ = next_screen_mode_;
			blank_screen_ = next_blank_screen_;
			switch(screen_mode_) {
				case 2:
					line_mode_ = LineMode::Text;
					first_pixel_column_ = 69;
					first_right_border_column_ = 309;
				break;
				default:
					line_mode_ = LineMode::Character;
					first_pixel_column_ = 63;
					first_right_border_column_ = 319;
				break;
			}
		}
	}
}

void TMS9918::output_border(int cycles) {
	pixel_target_ = reinterpret_cast<uint32_t *>(crt_->allocate_write_area(1));
	if(pixel_target_) *pixel_target_ = palette[background_colour_];
	crt_->output_level(static_cast<unsigned int>(cycles) * 4);
}

// TODO: as a temporary development measure, memory access below is magically instantaneous. Correct that.

void TMS9918::set_register(int address, uint8_t value) {
	// Writes to address 0 are writes to the video RAM. Store
	// the value and return.
	if(!(address & 1)) {
		write_phase_ = false;
		read_ahead_buffer_ = value;
		ram_[ram_pointer_ & 16383] = value;
		ram_pointer_++;
		return;
	}

	// Writes to address 1 are performed in pairs; if this is the
	// low byte of a value, store it and wait for the high byte.
	if(!write_phase_) {
		low_write_ = value;
		write_phase_ = true;
		return;
	}

	write_phase_ = false;
	if(value & 0x80) {
		// This is a write to a register.
		switch(value & 7) {
			case 0:
				next_screen_mode_ = (next_screen_mode_ & 6) | ((low_write_ & 2) >> 1);
//				printf("NSM: %02x\n", next_screen_mode_);
			break;

			case 1:
				next_blank_screen_ = !(low_write_ & 0x40);
				generate_interrupts_ = !!(low_write_ & 0x20);
				next_screen_mode_ = (next_screen_mode_ & 1) | ((low_write_ & 0x18) >> 3);
				sprites_16x16_ = !!(low_write_ & 0x02);
				sprites_magnified_ = !!(low_write_ & 0x01);

				sprite_height_ = 8;
				if(sprites_16x16_) sprite_height_ <<= 1;
				if(sprites_magnified_) sprite_height_ <<= 1;
//				printf("NSM: %02x\n", next_screen_mode_);
			break;

			case 2:
				pattern_name_address_ = static_cast<uint16_t>((low_write_ & 0xf) << 10);
			break;

			case 3:
				colour_table_address_ = static_cast<uint16_t>(low_write_ << 6);
			break;

			case 4:
				pattern_generator_table_address_ = static_cast<uint16_t>((low_write_ & 0x07) << 11);
			break;

			case 5:
				sprite_attribute_table_address_ = static_cast<uint16_t>((low_write_ & 0x7f) << 7);
			break;

			case 6:
				sprite_generator_table_address_ = static_cast<uint16_t>((low_write_ & 0x07) << 11);
			break;

			case 7:
				text_colour_ = low_write_ >> 4;
				background_colour_ = low_write_ & 0xf;
			break;
		}
	} else {
		// This is a write to the RAM pointer.
		ram_pointer_ = static_cast<uint16_t>(low_write_ | (value << 8));
		if(!(value & 0x40)) {
			// Officially a 'read' set, so perform lookahead.
			read_ahead_buffer_ = ram_[ram_pointer_ & 16383];
			ram_pointer_++;
		}
	}
}

uint8_t TMS9918::get_register(int address) {
	write_phase_ = false;

	// Reads from address 0 read video RAM, via the read-ahead buffer.
	if(!(address & 1)) {
		uint8_t result = read_ahead_buffer_;
		read_ahead_buffer_ = ram_[ram_pointer_ & 16383];
		ram_pointer_++;
		return result;
	}

	// Reads from address 1 get the status register.
	uint8_t result = status_;
	status_ &= ~(StatusInterrupt | StatusFifthSprite | StatusSpriteCollision);
	return result;
}

 HalfCycles TMS9918::get_time_until_interrupt() {
	if(!generate_interrupts_) return HalfCycles(-1);
	if(get_interrupt_line()) return HalfCycles(0);

	const int half_cycles_per_frame = frame_lines_ * 228 * 2;
	int half_cycles_remaining = (192 * 228 * 2 + half_cycles_per_frame - half_cycles_into_frame_.as_int()) % half_cycles_per_frame;
	return HalfCycles(half_cycles_remaining);
}

bool TMS9918::get_interrupt_line() {
	return (status_ & StatusInterrupt) && generate_interrupts_;
}