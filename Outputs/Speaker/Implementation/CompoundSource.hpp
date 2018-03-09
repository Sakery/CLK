//
//  CompoundSource.hpp
//  Clock Signal
//
//  Created by Thomas Harte on 19/12/2017.
//  Copyright © 2017 Thomas Harte. All rights reserved.
//

#ifndef CompoundSource_h
#define CompoundSource_h

#include "SampleSource.hpp"

#include <cstring>

namespace Outputs {
namespace Speaker {

/*!
	A CompoundSource adds together the sound generated by multiple individual SampleSources.
	It's an instance of template metaprogramming; this is the base case.
*/
template <typename... T> class CompoundSource: public Outputs::Speaker::SampleSource {
	public:
		void get_samples(std::size_t number_of_samples, std::int16_t *target) {
			std::memset(target, 0, sizeof(std::int16_t) * number_of_samples);
		}

		void set_scaled_volume_range(int16_t range) {}

		int size() {
			return 0;
		}
};

/*!
	A CompoundSource adds together the sound generated by multiple individual SampleSources.
	It's an instance of template metaprogramming; this is the recursive case.
*/
template <typename T, typename... R> class CompoundSource<T, R...>:
	public Outputs::Speaker::SampleSource {
	public:
		CompoundSource(T &source, R &...next) : source_(source), next_source_(next...) {}

		void get_samples(std::size_t number_of_samples, std::int16_t *target) {
			if(source_.is_zero_level()) {
				source_.skip_samples(number_of_samples);
				next_source_.get_samples(number_of_samples, target);
			} else {
				int16_t next_samples[number_of_samples];
				next_source_.get_samples(number_of_samples, next_samples);
				source_.get_samples(number_of_samples, target);
				while(number_of_samples--) {
					target[number_of_samples] += next_samples[number_of_samples];
				}
			}
		}

		void skip_samples(const std::size_t number_of_samples) {
			source_.skip_samples(number_of_samples);
			next_source_.skip_samples(number_of_samples);
		}

		void set_sample_volume_range(int16_t range) {
			set_scaled_volume_range(range / size());
		}

		void set_scaled_volume_range(int16_t range) {
			source_.set_sample_volume_range(range);
			next_source_.set_scaled_volume_range(range);
		}

		int size() {
			return 1+next_source_.size();
		}

	private:
		bool is_sleeping_ = false;
		T &source_;
		CompoundSource<R...> next_source_;
};

}
}

#endif /* CompoundSource_h */
