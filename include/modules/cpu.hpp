#pragma once

#include "settings.hpp"
#include "modules/meta/timer_module.hpp"

POLYBAR_NS

namespace modules {
  struct cpu_time {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long total;
  };

  using cpu_time_t = unique_ptr<cpu_time>;

	enum class cpu_state { NORMAL = 0, WARN, CRIT };

  class cpu_module : public timer_module<cpu_module> {
   public:
    explicit cpu_module(const bar_settings&, string);

    bool update();
		string get_format() const;
    bool build(builder* builder, const string& tag) const;

   protected:
    bool read_values();
    float get_load(size_t core) const;
		void prepare_label(cpu_state state, const string& tag);

   private:
    static constexpr auto TAG_LABEL = "<label>";
		static constexpr auto TAG_WARN_LABEL = "<warn-label>";
		static constexpr auto TAG_CRIT_LABEL = "<critical-label>";
    static constexpr auto TAG_BAR_LOAD = "<bar-load>";
    static constexpr auto TAG_RAMP_LOAD = "<ramp-load>";
    static constexpr auto TAG_RAMP_LOAD_PER_CORE = "<ramp-coreload>";
		static constexpr auto FORMAT_WARN = "format-warn";
		static constexpr auto FORMAT_CRITICAL = "format-critical";

    progressbar_t m_barload;
    ramp_t m_rampload;
    ramp_t m_rampload_core;
		map<cpu_state, label_t> m_labels;

		float m_totalwarn = 0.0f;
		float m_totalcritical = 0.0f;

    vector<cpu_time_t> m_cputimes;
    vector<cpu_time_t> m_cputimes_prev;

    float m_total = 0;
    vector<float> m_load;
  };
}

POLYBAR_NS_END
