#include "cable_test/soak.hpp"

namespace cable_test {

void soak_accumulate(SoakSummary& summary, const Analysis& analysis) noexcept {
    if (summary.runs == 0) {
        summary.first_kind = analysis.kind;
        summary.last_kind = analysis.kind;
    } else if (analysis.kind != summary.last_kind) {
        ++summary.kind_changes;
        summary.last_kind = analysis.kind;
    }

    ++summary.runs;
    if (analysis.idle_activity) {
        ++summary.idle_activity_runs;
    }

    for (std::size_t line = 0; line < kLineCount; ++line) {
        SoakLineStats& stats = summary.lines[line];
        ++stats.runs;
        if (analysis.open[line]) {
            ++stats.open;
        }
        if (line_shorted(analysis, line)) {
            ++stats.shorted;
        }
        if (analysis.mapping[line]) {
            ++stats.connected;
            stats.targets = static_cast<LineMask>(
                stats.targets | line_bit(static_cast<std::size_t>(*analysis.mapping[line])));
        }
    }
}

bool soak_line_stable(const SoakLineStats& stats) noexcept {
    return stats.runs > 0 && stats.connected == stats.runs && stats.open == 0 &&
           stats.shorted == 0 && popcount8(stats.targets) == 1;
}

bool soak_all_stable(const SoakSummary& summary) noexcept {
    if (summary.runs == 0 || summary.idle_activity_runs > 0 || summary.kind_changes > 0) {
        return false;
    }
    for (const SoakLineStats& stats : summary.lines) {
        if (!soak_line_stable(stats)) {
            return false;
        }
    }
    return true;
}

} // namespace cable_test
