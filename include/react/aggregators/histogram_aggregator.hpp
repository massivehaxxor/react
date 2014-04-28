/*
* 2014+ Copyright (c) Andrey Kashin <kashin.andrej@gmail.com>
* All rights reserved.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU Lesser General Public License for more details.
*/

#ifndef HISTOGRAM_AGGREGATOR_HPP
#define HISTOGRAM_AGGREGATOR_HPP

#include <limits>

#include "aggregator.hpp"

namespace react {

struct bucket_t {
	void update() {
		++frequency;
	}

	size_t get() const {
		return frequency;
	}

	void to_json(rapidjson::Value &value, rapidjson::Document::AllocatorType &allocator) const {
		value.AddMember("frequency", frequency, allocator);
	}

	size_t frequency;
};

template<typename Bucket>
class histogram_t {
public:
	template<typename ...Ticks>
	histogram_t(std::vector<int> dimensionTicks, Ticks... dimensionsTicks):
		ticks(dimensionTicks), buckets(dimensionTicks.size() + 1, Bucket(dimensionsTicks...)) {
		ticks.push_back(std::numeric_limits<int>::max());
	}

	template<typename ...Measurements>
	void update(int measurement, Measurements... measurements) {
		auto it = std::upper_bound(ticks.begin(), ticks.end(), measurement);
		size_t position = std::distance(ticks.begin(), it);
		buckets[position].update(measurements...);
	}

	template<typename ...Measurements>
	size_t get(int measurement, Measurements... measurements) const {
		return buckets[measurement].get(measurements...);
	}

	size_t get() const {
		return 0;
	}

	void to_json(rapidjson::Value &value, rapidjson::Document::AllocatorType &allocator) const {
		for (size_t i = 0; i < ticks.size(); ++i) {
			std::string label = "<" + std::to_string(static_cast<long long>(ticks[i]));

			if (std::is_same<Bucket, bucket_t>::value) {
				rapidjson::Value bucket_value(buckets[i].get());
				value.AddMember(label.c_str(), allocator, bucket_value, allocator);
			} else {
				rapidjson::Value bucket_value(rapidjson::kObjectType);
				buckets[i].to_json(bucket_value, allocator);
				value.AddMember(label.c_str(), allocator, bucket_value, allocator);
			}
		}
	}

private:
	std::vector<int> ticks;
	std::vector<Bucket> buckets;
};

typedef histogram_t<bucket_t> histogram1D_t;
typedef histogram_t<histogram1D_t> histogram2D_t;

template<typename HistogramType>
struct histogram_updater_t {
	histogram_updater_t() {}
	virtual ~histogram_updater_t() {}

	virtual void operator () (HistogramType &histogram, const call_tree_t &call_tree) const {
		operator()(histogram, call_tree, call_tree.get_action_codes_to_nodes_map());
	}

	virtual void operator () (HistogramType &histogram, const call_tree_t &call_tree,
							  const call_tree_t::code_to_node_map &action_codes_to_nodes_map) const = 0;


	virtual void to_json(rapidjson::Value &value,
						 rapidjson::Document::AllocatorType &allocator,
						 const actions_set_t &) const = 0;
};

struct action_time_histogram_updater_t : public histogram_updater_t<histogram1D_t> {
	typedef histogram1D_t HistogramType;

	action_time_histogram_updater_t(int action_code): action_code(action_code) {}
	~action_time_histogram_updater_t() {}

	void operator () (HistogramType &histogram, const call_tree_t &call_tree,
					  const call_tree_t::code_to_node_map &action_codes_to_nodes_map) const {
		auto action_code_nodes = action_codes_to_nodes_map.find(action_code);
		if (action_code_nodes != action_codes_to_nodes_map.end()) {
			for (auto it = action_code_nodes->second.begin(); it != action_code_nodes->second.end(); ++it) {
				int64_t delta = call_tree.get_node_stop_time(*it) - call_tree.get_node_start_time(*it);
				histogram.update(delta);
			}
		}
	}

	void to_json(rapidjson::Value &value,
				 rapidjson::Document::AllocatorType &allocator,
				 const actions_set_t &actions_set) const {
		value.AddMember("name", "action_time_updater", allocator);
		rapidjson::Value action_name_value(actions_set.get_action_name(action_code).c_str(), allocator);
		value.AddMember("action_name", action_name_value, allocator);
	}

	int action_code;
};

template<typename HistogramType>
class histogram_aggregator_t : public aggregator_t {
	typedef histogram_updater_t<HistogramType> HistogramUpdaterType;

public:
	template<typename ...Ticks>
	histogram_aggregator_t(const actions_set_t &actions_set,
						   std::shared_ptr<HistogramUpdaterType> histogram_updater,
						   Ticks ...ticks):
		aggregator_t(actions_set), histogram(ticks...), histogram_updater(histogram_updater) {}
	~histogram_aggregator_t() {}

	void aggregate(const call_tree_t &call_tree) {
		(*histogram_updater)(histogram, call_tree);
	}

	void aggregate(const call_tree_t &call_tree,
				   const call_tree_t::code_to_node_map &action_codes_to_nodes_map) {
		(*histogram_updater)(histogram, call_tree, action_codes_to_nodes_map);
	}

	void to_json(rapidjson::Value &value, rapidjson::Document::AllocatorType &allocator) const {
		rapidjson::Value histogram_aggregator_value(rapidjson::kObjectType);

		rapidjson::Value histogram_updater_value(rapidjson::kObjectType);
		histogram_updater->to_json(histogram_updater_value, allocator, actions_set);
		histogram_aggregator_value.AddMember("histogram_updater", histogram_updater_value, allocator);

		rapidjson::Value histogram_value(rapidjson::kObjectType);
		histogram.to_json(histogram_value, allocator);
		histogram_aggregator_value.AddMember("histogram", histogram_value, allocator);
		value.AddMember("histogram_aggregator", histogram_aggregator_value, allocator);
	}

private:
	HistogramType histogram;
	std::shared_ptr<HistogramUpdaterType> histogram_updater;
};

template<typename HistogramType>
class batch_histogram_aggregator_t : public aggregator_t {
	typedef histogram_aggregator_t<HistogramType> HistogramAggregatorType;
	typedef histogram_updater_t<HistogramType> HistogramUpdaterType;

public:
	batch_histogram_aggregator_t(const actions_set_t &actions_set): aggregator_t(actions_set) {}

	template<typename ...Ticks>
	void add_histogram_aggregator(std::shared_ptr<HistogramUpdaterType> histogram_updater,
								  Ticks ...ticks) {
		histogram_aggregators.emplace_back(
			new HistogramAggregatorType(actions_set, histogram_updater, ticks...)
		);
	}

	void aggregate(const call_tree_t &call_tree) {
		for (auto it = histogram_aggregators.begin(); it != histogram_aggregators.end(); ++it) {
			(*it)->aggregate(call_tree, call_tree.get_action_codes_to_nodes_map());
		}
	}

	void to_json(rapidjson::Value &value, rapidjson::Document::AllocatorType &allocator) const {
		rapidjson::Value batch_histogram_aggregator_value(rapidjson::kObjectType);

		rapidjson::Value histogram_aggregators_values(rapidjson::kArrayType);
		for (auto it = histogram_aggregators.begin(); it != histogram_aggregators.end(); ++it) {
			rapidjson::Value histogram_aggregator_value(rapidjson::kObjectType);
			(*it)->to_json(histogram_aggregator_value, allocator);
			histogram_aggregators_values.PushBack(histogram_aggregator_value, allocator);
		}
		batch_histogram_aggregator_value.AddMember("histogram_aggregators", histogram_aggregators_values, allocator);

		value.AddMember("batch_histogram_aggregator", batch_histogram_aggregator_value, allocator);
	}

private:
	std::vector<std::unique_ptr<HistogramAggregatorType>> histogram_aggregators;
};

} // namespace react

#endif // HISTOGRAM_AGGREGATOR_HPP
