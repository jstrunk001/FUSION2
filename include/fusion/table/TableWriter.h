#ifndef FUSION_TABLE_TABLEWRITER_H
#define FUSION_TABLE_TABLEWRITER_H

#include <string>
#include <vector>
#include <filesystem>
#include <memory>

namespace fusion::table {

// One named raster band's data, referenced (not owned) by the caller's
// existing vector -- mirrors the BandDef shape every raster-writing tool
// already builds right before GDALRaster::WriteBandData().
struct BandDef {
    std::string name;
    const std::vector<float>* data{nullptr};
};

// Format is dispatched purely by outputPath's extension:
//   .csv                    -> plain-text CSV
//   .sqlite / .db / .sqlite3 -> SQLite table (single table named "grid")
// Returns false (and prints an error to stderr) for any other extension, a
// failure to open/create the output, or a mismatched bands[*].data size.
//
// Writes one row per grid cell in row-major (row, then col) order:
// col,row,x,y,<band1>,<band2>,...  X/Y are cell-center coordinates derived
// from geotransform, matching the convention gridmetrics.cpp's existing CSV
// export already uses.  A cell where every band's value equals noDataValue
// is still written (not skipped) -- callers that want "empty" rows dropped
// should filter bands themselves before calling.
bool WriteGridTable(const std::filesystem::path& outputPath,
                     int cols, int rows,
                     const double geotransform[6],
                     const std::vector<BandDef>& bands,
                     float noDataValue);

// Concatenates several per-tile tables written by WriteGridTable (same
// format, same column schema) into one output table at outputPath, in the
// given order (callers are responsible for passing tileTablePaths already
// sorted into a stable, deterministic order -- e.g. ascending tile ID).
//
// CSV: copies the first input file's header line once, then every data row
// (skipping each subsequent file's own header line).
// SQLite: creates outputPath fresh and INSERTs every row from every input
// database's "grid" table, each source file's rows wrapped in one
// transaction for insert throughput.
//
// Format is dispatched by outputPath's extension, same rule as
// WriteGridTable. All tileTablePaths must share outputPath's extension.
bool ConcatenateGridTables(const std::filesystem::path& outputPath,
                           const std::vector<std::filesystem::path>& tileTablePaths);

// Incremental writer for a table whose columns aren't a flat per-cell band
// list -- e.g. gridmetrics' rich per-cell CSV (elevation stat bundle,
// strata sub-tables, spectral, experimental metrics), where each row's
// column set is assembled by bespoke logic rather than one vector per band.
// Every column is written as a number; NaN means "no value" -- rendered as
// the literal text "NA" for CSV, or a real SQL NULL for SQLite, matching
// the sentinel convention gridmetrics' existing CSV export already uses
// (a value is NaN exactly when it resolved to an NA-resolved /nodata or
// /noheight sentinel).  Format is dispatched by the output path's
// extension, same rule as WriteGridTable.
class RowTableWriter {
public:
    RowTableWriter();
    ~RowTableWriter();
    RowTableWriter(const RowTableWriter&) = delete;
    RowTableWriter& operator=(const RowTableWriter&) = delete;

    // columnNames must be unique (SQLite column names) and non-empty.
    bool Open(const std::filesystem::path& outputPath, const std::vector<std::string>& columnNames);

    // values.size() must equal the columnNames passed to Open().
    bool WriteRow(const std::vector<double>& values);

    bool Close();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace fusion::table

#endif // FUSION_TABLE_TABLEWRITER_H
