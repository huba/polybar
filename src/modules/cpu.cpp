#include <fstream>
#include <istream>

#include "modules/cpu.hpp"

#include "drawtypes/label.hpp"
#include "drawtypes/progressbar.hpp"
#include "drawtypes/ramp.hpp"
#include "utils/math.hpp"

#include "modules/meta/base.inl"

POLYBAR_NS

namespace modules {
  template class module<cpu_module>;

  cpu_module::cpu_module(const bar_settings& bar, string name_) : timer_module<cpu_module>(bar, move(name_)) {
    m_interval = m_conf.get<decltype(m_interval)>(name(), "interval", 1s);

		m_totalwarn = m_conf.get(name(), "warn-percentage", 70.0f);
		m_totalcritical = m_conf.get(name(), "critical-percentage", 90.0f);

    m_formatter->add(DEFAULT_FORMAT, TAG_LABEL, {TAG_LABEL, TAG_BAR_LOAD, TAG_RAMP_LOAD, TAG_RAMP_LOAD_PER_CORE});

		string fallback_value = m_formatter->get(DEFAULT_FORMAT)->value;
    m_formatter->add(FORMAT_WARN, fallback_value, {TAG_LABEL, TAG_WARN_LABEL, TAG_BAR_LOAD, TAG_RAMP_LOAD, TAG_RAMP_LOAD_PER_CORE});
    m_formatter->add(FORMAT_CRITICAL, fallback_value, {TAG_LABEL, TAG_CRIT_LABEL, TAG_BAR_LOAD, TAG_RAMP_LOAD, TAG_RAMP_LOAD_PER_CORE});

    // warmup cpu times
    read_values();
    read_values();

    if (m_formatter->has(TAG_BAR_LOAD)) {
      m_barload = load_progressbar(m_bar, m_conf, name(), TAG_BAR_LOAD);
    }
    if (m_formatter->has(TAG_RAMP_LOAD)) {
      m_rampload = load_ramp(m_conf, name(), TAG_RAMP_LOAD);
    }
    if (m_formatter->has(TAG_RAMP_LOAD_PER_CORE)) {
      m_rampload_core = load_ramp(m_conf, name(), TAG_RAMP_LOAD_PER_CORE);
    }
    if (m_formatter->has(TAG_LABEL)) {
			prepare_label(cpu_state::NORMAL, TAG_LABEL);
    }
    if (m_formatter->has(TAG_WARN_LABEL)) {
			prepare_label(cpu_state::WARN, TAG_WARN_LABEL);
    }
    if (m_formatter->has(TAG_CRIT_LABEL)) {
			prepare_label(cpu_state::CRIT, TAG_CRIT_LABEL);
    }
  }

	void cpu_module::prepare_label(cpu_state state, const string& tag) {
		// Update the label parameter and replace the %percentag-cores% token with the individual core tokens
		string key{&tag[1], tag.length() - 2};
		auto raw_label = m_conf.get<string>(name(), key, "%percentage%%");
		vector<string> cores;
		for (size_t i = 1; i <= m_cputimes.size(); i++) {
			cores.emplace_back("%percentage-core" + to_string(i) + "%%");
		}
		raw_label = string_util::replace_all(raw_label, "%percentage-cores%", string_util::join(cores, " "));
		const_cast<config&>(m_conf).set(name(), key, move(raw_label));

		label_t label = load_optional_label(m_conf, name(), tag, "%percentage%%");

		if (label) {
			m_labels[state] = label;
		}
	}

  bool cpu_module::update() {
    if (!read_values()) {
      return false;
    }

    m_total = 0.0f;
    m_load.clear();

    auto cores_n = m_cputimes.size();
    if (!cores_n) {
      return false;
    }

    vector<string> percentage_cores;
    for (size_t i = 0; i < cores_n; i++) {
      auto load = get_load(i);
      m_total += load;
      m_load.emplace_back(load);

      if (!m_labels.empty()) {
				percentage_cores.emplace_back(to_string(static_cast<int>(load + 0.5)));
      }
    }

    m_total = m_total / static_cast<float>(cores_n);

		for (auto& pair: m_labels) {
			label_t label = pair.second;
			if (label) {
				label->reset_tokens();
				label->replace_token("%percentage%", to_string(static_cast<int>(m_total + 0.5)));

				for (size_t i = 0; i < percentage_cores.size(); i++) {
					label->replace_token("%percentage-core" + to_string(i + 1) + "%", percentage_cores[i]);
				}
 
			}
		}

    return true;
  }

	string cpu_module::get_format() const {
		if (m_total > m_totalcritical) {
			return FORMAT_CRITICAL;
		} else if (m_total > m_totalwarn) {
			return FORMAT_WARN;
		} else {
			return DEFAULT_FORMAT;
		}
	}

  bool cpu_module::build(builder* builder, const string& tag) const {
    if (tag == TAG_LABEL) {
      builder->node(m_labels.at(cpu_state::NORMAL));
		} else if (tag == TAG_WARN_LABEL) {
      builder->node(m_labels.at(cpu_state::WARN));
		} else if (tag == TAG_CRIT_LABEL) {
      builder->node(m_labels.at(cpu_state::CRIT));
    } else if (tag == TAG_BAR_LOAD) {
      builder->node(m_barload->output(m_total));
    } else if (tag == TAG_RAMP_LOAD) {
      builder->node(m_rampload->get_by_percentage(m_total));
    } else if (tag == TAG_RAMP_LOAD_PER_CORE) {
      auto i = 0;
      for (auto&& load : m_load) {
        if (i++ > 0) {
          builder->space(1);
        }
        builder->node(m_rampload_core->get_by_percentage(load));
      }
      builder->node(builder->flush());
    } else {
      return false;
    }
    return true;
  }

  bool cpu_module::read_values() {
    m_cputimes_prev.swap(m_cputimes);
    m_cputimes.clear();

    try {
      std::ifstream in(PATH_CPU_INFO);
      string str;

      while (std::getline(in, str) && str.compare(0, 3, "cpu") == 0) {
        // skip line with accumulated value
        if (str.compare(0, 4, "cpu ") == 0) {
          continue;
        }

        auto values = string_util::split(str, ' ');

        m_cputimes.emplace_back(new cpu_time);
        m_cputimes.back()->user = std::stoull(values[1], nullptr, 10);
        m_cputimes.back()->nice = std::stoull(values[2], nullptr, 10);
        m_cputimes.back()->system = std::stoull(values[3], nullptr, 10);
        m_cputimes.back()->idle = std::stoull(values[4], nullptr, 10);
        m_cputimes.back()->total =
            m_cputimes.back()->user + m_cputimes.back()->nice + m_cputimes.back()->system + m_cputimes.back()->idle;
      }
    } catch (const std::ios_base::failure& e) {
      m_log.err("Failed to read CPU values (what: %s)", e.what());
    }

    return !m_cputimes.empty();
  }

  float cpu_module::get_load(size_t core) const {
    if (m_cputimes.empty() || m_cputimes_prev.empty()) {
      return 0;
    } else if (core >= m_cputimes.size() || core >= m_cputimes_prev.size()) {
      return 0;
    }

    auto& last = m_cputimes[core];
    auto& prev = m_cputimes_prev[core];

    auto last_idle = last->idle;
    auto prev_idle = prev->idle;

    auto diff = last->total - prev->total;

    if (diff == 0) {
      return 0;
    }

    float percentage = 100.0f * (diff - (last_idle - prev_idle)) / diff;

    return math_util::cap<float>(percentage, 0, 100);
  }
}

POLYBAR_NS_END
