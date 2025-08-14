#include "perf/tightloop_lib.h"

#include <sched.h>
#include <stdint.h>

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "perf/bits.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

ABSL_FLAG(int32_t, processor_affinity, -1,
          "The processor to bind to and only run on");
ABSL_FLAG(absl::Duration, run_duration, absl::Seconds(3),
          "The duration of the sampling loop.");

ABSL_FLAG(int64_t, cycles_min, -1,
          "All samples smaller than cycles_min cycles will be grouped into the "
          "first bucket.");
ABSL_FLAG(int64_t, cycles_shift, -1,
          "This value controls the size of the first bucket. Each subsequent "
          "bucket is 2 times larger than the preceding one. Use this value to "
          "reduce the number of buckets at the cost of granularity.");

ABSL_FLAG(absl::Duration, duration_min, absl::Microseconds(-1),
          "All elapsed smaller than duration_min will be grouped in the first "
          "bucket.");
ABSL_FLAG(absl::Duration, duration_shift, absl::Microseconds(-1),
          "This value controls the size of the first bucket.");

ABSL_FLAG(absl::Duration, sleep_duration, absl::Microseconds(-1), "");
ABSL_FLAG(bool, exclude_sleep, false,
          "Specifies whether the sleep duration should be excluded from the "
          "histogram");

ABSL_FLAG(bool, print_csv, false, "Print the histogram in CSV format.");


// Returns [cycles_min, cycles_shift]
std::pair<uint64_t, uint64_t> SetupEnvironment() {
  // All values smaller than cycles_min will be grouped
  int64_t cycles_min = 0;
  // The scale shift for values larger than cycles_min
  int64_t cycles_shift = 0;

  auto processor_affinity = absl::GetFlag(FLAGS_processor_affinity);
  if (processor_affinity != -1) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(processor_affinity, &mask);
    sched_setaffinity(0, sizeof(mask), &mask);
  }

  auto flag_cycles_min = absl::GetFlag(FLAGS_cycles_min);
  auto flag_cycles_shift = absl::GetFlag(FLAGS_cycles_shift);

  auto flag_dur_min = absl::GetFlag(FLAGS_duration_min);
  auto flag_dur_shift = absl::GetFlag(FLAGS_duration_shift);

  if (flag_cycles_min >= 0) {
    cycles_min = flag_cycles_min;
  } else if (flag_dur_min >= absl::ZeroDuration()) {
    cycles_min = DurationToCycles(flag_dur_min);
  } else {
    cycles_min = DurationToCycles(absl::Microseconds(1));
    LOG(INFO) << "cycles_min = " << cycles_min;
  }

  if (flag_cycles_shift >= 0) {
    // If explicit shift was specified, use it.
    cycles_shift = flag_cycles_shift;
  } else if (flag_dur_shift >= absl::ZeroDuration()) {
    // If explicit shift in microseconds was specified use it.
    cycles_shift =
        mogo::Fls64(DurationToCycles(flag_dur_shift));
  } else if (flag_dur_min >= absl::ZeroDuration()) {
    // If we're interested in microseconds, use microsecond granularity.
    cycles_shift =
        mogo::Fls64(DurationToCycles(absl::Microseconds(1)));
  } else {
    cycles_shift = mogo::Fls64(cycles_min);
    LOG(INFO) << "cycles_shift = " << cycles_shift;
  }
  return std::make_pair(cycles_min, cycles_shift);
}

// AI generated code, so guaranteed to be perfect:
// https://g.co/gemini/share/bb324adba2ea
class ConsoleTable {
 public:
  // Constructor to set header and optional formatting options
  explicit ConsoleTable(const std::vector<std::string>& headers,
                        char borderChar = ' ', char paddingChar = ' ',
                        bool showInnerLines = false)
      : column_headers_(headers),
        border_character_(borderChar),
        padding_character_(paddingChar),
        display_inner_lines_(showInnerLines) {
    CalculateColumnWidths();
  }

  // Add a row of data
  void AddRow(const std::vector<std::string>& rowData) {
    rows_.push_back(rowData);
    CalculateColumnWidths();  // Recalculate in case new data is wider
  }

  // Print the formatted table to the console
  void Print() const {
    PrintRow(column_headers_);
    PrintHorizontalBorder();

    for (size_t i = 0; i < rows_.size(); ++i) {
      PrintRow(rows_[i]);
      if (display_inner_lines_ && i < rows_.size() - 1) {
        PrintHorizontalBorder();
      }
    }

    PrintHorizontalBorder();
  }

 private:
  // Calculate widths of columns based on header and data
  void CalculateColumnWidths() {
    column_widths_.clear();
    for (size_t i = 0; i < column_headers_.size(); ++i) {
      int maxWidth = column_headers_[i].size();
      for (const auto& row : rows_) {
        if (i < row.size()) {
          maxWidth = std::max(maxWidth, (int)row[i].size());
        }
      }
      column_widths_.push_back(maxWidth);
    }
  }

  // Print a horizontal border line
  void PrintHorizontalBorder() const {
    for (int width : column_widths_) {
      std::cout << std::setfill('-') << std::setw(width + 2) << "";
    }
    std::cout << std::endl;
  }

  // Print a single row of data
  void PrintRow(const std::vector<std::string>& row) const {
    for (size_t i = 0; i < column_widths_.size(); ++i) {
      if (i < row.size()) {
        std::cout << row[i];
      }
      std::cout << std::setfill(padding_character_)
                << std::setw(column_widths_[i] -
                             (i < row.size() ? row[i].length() : 0))
                << "" << padding_character_ << border_character_;
    }
    std::cout << std::endl;
  }

  std::vector<std::string> column_headers_;
  std::vector<std::vector<std::string>> rows_;
  std::vector<int> column_widths_;
  char border_character_;
  char padding_character_;
  bool display_inner_lines_;
};

void PrintHistogram(mogo::Histogram64& h) {
  int first_nonzero_bucket = 0;
  while (first_nonzero_bucket <= h.max_pos() &&
         h.value_at_pos(first_nonzero_bucket) == 0) {
    ++first_nonzero_bucket;
  }

  int last_nonzero_bucket = h.max_pos();
  while (last_nonzero_bucket >= first_nonzero_bucket &&
         h.value_at_pos(last_nonzero_bucket) == 0) {
    --last_nonzero_bucket;
  }

  int64_t total_sample_count = h.total();

  LOG(INFO) << "Cycles per microsecond: " << DurationToCycles(absl::Microseconds(1));

  if (absl::GetFlag(FLAGS_print_csv)) {
    std::cout << "CSV data:" << std::endl;
    std::cout << "Count,Cycles,Microseconds" << std::endl;
    for (int i = first_nonzero_bucket; i <= last_nonzero_bucket; ++i) {
      std::cout << h.value_at_pos(i) << "," << h.range_max_pos(i) << ","
                << absl::ToDoubleMicroseconds(
                       CyclesToDuration(h.range_max_pos(i)))
                << std::endl;
    }
    std::cout << std::endl << "Human readable data:" << std::endl;
  }

  ConsoleTable table({"Count", "Cyc", "Microseconds", "% tot", "% this"});
  uint64_t running_sample_count = 0;
  for (int i = first_nonzero_bucket; i <= last_nonzero_bucket; ++i) {
    running_sample_count += h.value_at_pos(i);
    if (i == 0) {
      table.AddRow(
          {absl::StrCat(h.value_at_pos(i)),
           absl::StrCat(h.range_max_pos(i), " cyc"),
           absl::StrCat(CyclesToDuration(h.range_max_pos(i))),
           absl::StrCat(h.value_at_pos(i) * 100 / total_sample_count),
           absl::StrCat(h.value_at_pos(i) * 100 / total_sample_count)});
    } else {
      table.AddRow(
          {absl::StrCat(h.value_at_pos(i)),
           absl::StrCat(h.range_min_pos(i), " cyc - ", h.range_max_pos(i),
                        " cyc"),
           absl::StrCat(CyclesToDuration(h.range_min_pos(i)),
                        " - ",
                        CyclesToDuration(h.range_max_pos(i))),
           absl::StrCat(running_sample_count * 100 / total_sample_count),
           absl::StrCat(h.value_at_pos(i) * 100 / total_sample_count)});
    }
  }
  table.Print();
}
