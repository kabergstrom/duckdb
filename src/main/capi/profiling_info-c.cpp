#include "duckdb/main/capi/capi_internal.hpp"

using duckdb::Connection;
using duckdb::DuckDB;
using duckdb::EnumUtil;
using duckdb::MetricsType;
using duckdb::ClientContext;
using duckdb::optional_ptr;
using duckdb::ProfilingNode;
using duckdb::QueryProfiler;

duckdb_profiling_info duckdb_get_profiling_info(duckdb_connection connection) {
	if (!connection) {
		return nullptr;
	}
	Connection *conn = reinterpret_cast<Connection *>(connection);
	optional_ptr<ProfilingNode> profiling_node;
	try {
		profiling_node = conn->GetProfilingTree();
	} catch (std::exception &ex) {
		return nullptr;
	}

	if (!profiling_node) {
		return nullptr;
	}
	return reinterpret_cast<duckdb_profiling_info>(profiling_node.get());
}

duckdb_value duckdb_profiling_info_get_value(duckdb_profiling_info info, const char *key) {
	if (!info) {
		return nullptr;
	}
	auto &node = *reinterpret_cast<duckdb::ProfilingNode *>(info);
	auto &profiling_info = node.GetProfilingInfo();
	auto key_enum = EnumUtil::FromString<MetricsType>(duckdb::StringUtil::Upper(key));
	if (!profiling_info.Enabled(profiling_info.settings, key_enum)) {
		return nullptr;
	}

	auto str = profiling_info.GetMetricAsString(key_enum);
	return duckdb_create_varchar_length(str.c_str(), strlen(str.c_str()));
}

duckdb_value duckdb_profiling_info_get_metrics(duckdb_profiling_info info) {
	if (!info) {
		return nullptr;
	}

	auto &node = *reinterpret_cast<duckdb::ProfilingNode *>(info);
	auto &profiling_info = node.GetProfilingInfo();

	duckdb::InsertionOrderPreservingMap<duckdb::string> metrics_map;
	for (const auto &metric : profiling_info.metrics) {
		auto key = EnumUtil::ToString(metric.first);
		if (!profiling_info.Enabled(profiling_info.settings, metric.first)) {
			continue;
		}

		if (key == EnumUtil::ToString(MetricsType::OPERATOR_TYPE)) {
			auto type = duckdb::PhysicalOperatorType(metric.second.GetValue<uint8_t>());
			metrics_map[key] = EnumUtil::ToString(type);
		} else {
			metrics_map[key] = metric.second.ToString();
		}
	}

	auto map = duckdb::Value::MAP(metrics_map);
	return reinterpret_cast<duckdb_value>(new duckdb::Value(map));
}

idx_t duckdb_profiling_info_get_child_count(duckdb_profiling_info info) {
	if (!info) {
		return 0;
	}
	auto &node = *reinterpret_cast<duckdb::ProfilingNode *>(info);
	return node.GetChildCount();
}

duckdb_profiling_info duckdb_profiling_info_get_child(duckdb_profiling_info info, idx_t index) {
	if (!info) {
		return nullptr;
	}
	auto &node = *reinterpret_cast<duckdb::ProfilingNode *>(info);
	if (index >= node.GetChildCount()) {
		return nullptr;
	}

	ProfilingNode *profiling_info_ptr = node.GetChild(index).get();
	return reinterpret_cast<duckdb_profiling_info>(profiling_info_ptr);
}

static double sum_operator_cpu_time(duckdb::ProfilingNode &node) {
	double total = 0.0;
	auto &info = node.GetProfilingInfo();
	if (info.Enabled(info.settings, MetricsType::OPERATOR_CPU_TIME)) {
		auto it = info.metrics.find(MetricsType::OPERATOR_CPU_TIME);
		if (it != info.metrics.end()) {
			total += it->second.GetValue<double>();
		}
	}
	for (idx_t i = 0; i < node.GetChildCount(); i++) {
		auto child = node.GetChild(i);
		if (child) {
			total += sum_operator_cpu_time(*child);
		}
	}
	return total;
}

double duckdb_get_accumulated_cpu_time(duckdb_connection connection) {
	if (!connection) {
		return 0.0;
	}
	Connection *conn = reinterpret_cast<Connection *>(connection);
	double result = 0.0;
	try {
		auto &profiler = QueryProfiler::Get(*conn->context);
		profiler.GetRootUnderLock([&](optional_ptr<ProfilingNode> root) {
			if (root) {
				result = sum_operator_cpu_time(*root);
			}
		});
	} catch (std::exception &) {
		return 0.0;
	}
	return result;
}
