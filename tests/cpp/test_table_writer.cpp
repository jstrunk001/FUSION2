#include "fusion/table/TableWriter.h"
#include "test_assert.h"
#include "sqlite3.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace {

// Counts rows and sums one column of a "grid" SQLite table -- used to check
// that every row a writer produced actually made it to disk and back.
void SummarizeSqliteGridTable(const std::filesystem::path& path, const std::string& column,
                               long long& rowCount, double& columnSum) {
    rowCount = 0;
    columnSum = 0.0;
    sqlite3* db = nullptr;
    if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK) {
        sqlite3_close(db);
        return;
    }
    std::string sql = "SELECT COUNT(*), SUM(\"" + column + "\") FROM grid;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            rowCount = sqlite3_column_int64(stmt, 0);
            columnSum = sqlite3_column_double(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

long long CountCsvDataRows(const std::filesystem::path& path) {
    std::ifstream in(path);
    long long count = 0;
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first) { first = false; continue; } // header
        if (!line.empty()) ++count;
    }
    return count;
}

} // namespace

// TableWriter.cpp added SQLite bulk-insert pragmas (synchronous=OFF,
// journal_mode=MEMORY, temp_store=MEMORY, cache_size=100000) to speed up the
// grid-table export path -- see ApplyBulkInsertPragmas() in TableWriter.cpp.
// Those settings trade away crash-safety guarantees the code never relied on
// (every write here starts by deleting/replacing the output file), but they
// must not trade away correctness of a normal, uninterrupted run. These
// tests write real grid/row tables through the public WriteGridTable,
// RowTableWriter, and ConcatenateGridTables entry points -- the only ones
// that exercise the pragma'd connections -- and verify every row and value
// survives the round trip to disk and back.
int RunTableWriterTests() {
    int failures = 0;
    std::cout << "TableWriter tests\n";

    std::filesystem::path tmpDir = std::filesystem::temp_directory_path() / "fusion_tests_tablewriter";
    std::filesystem::create_directories(tmpDir);

    // WriteGridTable to SQLite: a 20x15 grid (300 cells) with two bands,
    // large enough that the bulk INSERT transaction actually spans many
    // statements rather than being a one-row degenerate case.
    {
        int cols = 20, rows = 15;
        size_t n = static_cast<size_t>(cols) * rows;
        std::vector<float> bandA(n), bandB(n);
        double expectedSumA = 0.0;
        for (size_t i = 0; i < n; ++i) {
            bandA[i] = static_cast<float>(i) * 0.5f;
            bandB[i] = static_cast<float>(i) * -1.0f;
            expectedSumA += bandA[i];
        }
        double geotransform[6] = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
        std::vector<fusion::table::BandDef> bands = {{"band_a", &bandA}, {"band_b", &bandB}};

        auto sqlitePath = tmpDir / "grid_table.sqlite";
        std::error_code ec;
        std::filesystem::remove(sqlitePath, ec);
        bool ok = fusion::table::WriteGridTable(sqlitePath, cols, rows, geotransform, bands, -9999.0f);
        CHECK(ok, failures);

        long long rowCount = 0;
        double sumA = 0.0;
        SummarizeSqliteGridTable(sqlitePath, "band_a", rowCount, sumA);
        CHECK(rowCount == static_cast<long long>(n), failures);
        CHECK(std::abs(sumA - expectedSumA) < 1e-3, failures);

        // Same data through the CSV path, as a cross-check that both formats
        // agree on row count for identical input.
        auto csvPath = tmpDir / "grid_table.csv";
        std::filesystem::remove(csvPath, ec);
        CHECK(fusion::table::WriteGridTable(csvPath, cols, rows, geotransform, bands, -9999.0f), failures);
        CHECK(CountCsvDataRows(csvPath) == static_cast<long long>(n), failures);
    }

    // RowTableWriter to SQLite: a NULL-handling check (NaN -> SQL NULL, not
    // a stray 0 or a broken row), plus a row-count check across a few
    // hundred rows to exercise the same bulk-insert transaction path.
    {
        auto sqlitePath = tmpDir / "row_table.sqlite";
        std::error_code ec;
        std::filesystem::remove(sqlitePath, ec);

        fusion::table::RowTableWriter writer;
        bool opened = writer.Open(sqlitePath, {"id", "value"});
        CHECK(opened, failures);

        const int numRows = 500;
        for (int i = 0; i < numRows; ++i) {
            double value = (i % 10 == 0) ? std::nan("") : static_cast<double>(i);
            CHECK(writer.WriteRow({static_cast<double>(i), value}), failures);
        }
        CHECK(writer.Close(), failures);

        long long rowCount = 0;
        double sumValue = 0.0;
        SummarizeSqliteGridTable(sqlitePath, "value", rowCount, sumValue);
        CHECK(rowCount == numRows, failures);

        // NULL count: every 10th row (0, 10, 20, ...) was written as NaN.
        sqlite3* db = nullptr;
        long long nullCount = -1;
        if (sqlite3_open(sqlitePath.string().c_str(), &db) == SQLITE_OK) {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM grid WHERE value IS NULL;", -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) nullCount = sqlite3_column_int64(stmt, 0);
                sqlite3_finalize(stmt);
            }
        }
        sqlite3_close(db);
        CHECK(nullCount == numRows / 10, failures);
    }

    // ConcatenateGridTables (SQLite): also runs through the pragma'd ATTACH
    // path in ConcatenateSQLite -- merging two per-tile tables must yield
    // the sum of their row counts, with no row dropped or duplicated.
    {
        int cols = 4, rows = 4;
        size_t n = static_cast<size_t>(cols) * rows;
        double geotransform[6] = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};

        std::vector<float> tileA(n, 1.0f), tileB(n, 2.0f);
        std::vector<fusion::table::BandDef> bandsA = {{"val", &tileA}};
        std::vector<fusion::table::BandDef> bandsB = {{"val", &tileB}};

        auto tileAPath = tmpDir / "tile_a.sqlite";
        auto tileBPath = tmpDir / "tile_b.sqlite";
        auto mergedPath = tmpDir / "merged.sqlite";
        std::error_code ec;
        std::filesystem::remove(tileAPath, ec);
        std::filesystem::remove(tileBPath, ec);
        std::filesystem::remove(mergedPath, ec);

        CHECK(fusion::table::WriteGridTable(tileAPath, cols, rows, geotransform, bandsA, -9999.0f), failures);
        CHECK(fusion::table::WriteGridTable(tileBPath, cols, rows, geotransform, bandsB, -9999.0f), failures);
        CHECK(fusion::table::ConcatenateGridTables(mergedPath, {tileAPath, tileBPath}), failures);

        long long rowCount = 0;
        double sumVal = 0.0;
        SummarizeSqliteGridTable(mergedPath, "val", rowCount, sumVal);
        CHECK(rowCount == static_cast<long long>(2 * n), failures);
        CHECK(std::abs(sumVal - (n * 1.0 + n * 2.0)) < 1e-6, failures);
    }

    std::filesystem::remove_all(tmpDir);

    std::cout << (failures == 0 ? "  all passed\n" : ("  " + std::to_string(failures) + " failure(s)\n"));
    return failures;
}
