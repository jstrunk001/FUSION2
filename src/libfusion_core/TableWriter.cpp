#include "fusion/table/TableWriter.h"

#include "sqlite3.h"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace fusion::table {

namespace {

enum class TableFormat { CSV, SQLite, Unknown };

TableFormat DetectFormat(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (ext == ".csv") return TableFormat::CSV;
    if (ext == ".sqlite" || ext == ".db" || ext == ".sqlite3") return TableFormat::SQLite;
    return TableFormat::Unknown;
}

bool ValidateBandSizes(const std::vector<BandDef>& bands, size_t expectedSize) {
    for (const auto& band : bands) {
        if (band.data == nullptr || band.data->size() != expectedSize) {
            std::cerr << "Error: table band '" << band.name << "' has " << (band.data ? band.data->size() : 0)
                      << " values, expected " << expectedSize << " (cols*rows).\n";
            return false;
        }
    }
    return true;
}

// col/row -> cell-center X/Y, matching gridmetrics.cpp's existing CSV
// export convention: geotransform = {originX, pixelWidth, 0, originY, 0, -pixelHeight}.
void CellCenter(const double geotransform[6], int col, int row, double& x, double& y) {
    x = geotransform[0] + (col + 0.5) * geotransform[1];
    y = geotransform[3] + (row + 0.5) * geotransform[5];
}

bool WriteGridTableCSV(const std::filesystem::path& outputPath, int cols, int rows,
                       const double geotransform[6], const std::vector<BandDef>& bands) {
    std::ofstream csv(outputPath);
    if (!csv.is_open()) {
        std::cerr << "Error: could not open '" << outputPath.string() << "' for writing.\n";
        return false;
    }

    csv << "col,row,x,y";
    for (const auto& band : bands) {
        csv << "," << band.name;
    }
    csv << "\n";

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            double x, y;
            CellCenter(geotransform, col, row, x, y);
            size_t idx = static_cast<size_t>(row) * cols + col;
            csv << col << "," << row << "," << x << "," << y;
            for (const auto& band : bands) {
                csv << "," << (*band.data)[idx];
            }
            csv << "\n";
        }
    }

    return true;
}

// RAII wrapper so every early-return path still closes the connection.
class SQLiteConnection {
public:
    explicit SQLiteConnection(sqlite3* db) : m_db(db) {}
    ~SQLiteConnection() { if (m_db) sqlite3_close(m_db); }
    SQLiteConnection(const SQLiteConnection&) = delete;
    SQLiteConnection& operator=(const SQLiteConnection&) = delete;
    sqlite3* get() const { return m_db; }
private:
    sqlite3* m_db;
};

// These tables are disposable, regenerate-from-source exports (each write
// starts by deleting any stale file, mirroring WriteGridTableSQLite's own
// "start fresh" comment below) rather than a durable system of record, so
// the crash-safety SQLite's defaults exist for is not needed here. Default
// journaling (DELETE) and synchronous=FULL force an fsync-heavy flush on
// every transaction commit; for a bulk, single-run export that trade only
// slows the write down without protecting anything worth protecting.
void ApplyBulkInsertPragmas(sqlite3* db) {
    sqlite3_exec(db, "PRAGMA synchronous = OFF;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA journal_mode = MEMORY;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA temp_store = MEMORY;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA cache_size = 100000;", nullptr, nullptr, nullptr);
}

bool ExecSQL(sqlite3* db, const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Error: SQLite statement failed: " << (errMsg ? errMsg : "unknown error") << "\n";
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool WriteGridTableSQLite(const std::filesystem::path& outputPath, int cols, int rows,
                          const double geotransform[6], const std::vector<BandDef>& bands,
                          const std::string& tableName = "grid") {
    std::error_code ec;
    std::filesystem::remove(outputPath, ec); // start fresh -- a stale file with an incompatible schema must not linger

    sqlite3* rawDb = nullptr;
    if (sqlite3_open(outputPath.string().c_str(), &rawDb) != SQLITE_OK) {
        std::cerr << "Error: could not create SQLite database '" << outputPath.string() << "': "
                  << (rawDb ? sqlite3_errmsg(rawDb) : "unknown error") << "\n";
        if (rawDb) sqlite3_close(rawDb);
        return false;
    }
    SQLiteConnection conn(rawDb);
    sqlite3* db = conn.get();
    ApplyBulkInsertPragmas(db);

    std::string createSQL = "CREATE TABLE " + tableName + " (col INTEGER, row INTEGER, x REAL, y REAL";
    for (const auto& band : bands) {
        createSQL += ", \"" + band.name + "\" REAL";
    }
    createSQL += ");";
    if (!ExecSQL(db, createSQL)) return false;

    std::string insertSQL = "INSERT INTO " + tableName + " VALUES (?,?,?,?";
    for (size_t k = 0; k < bands.size(); ++k) insertSQL += ",?";
    insertSQL += ");";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, insertSQL.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error: failed to prepare SQLite insert statement: " << sqlite3_errmsg(db) << "\n";
        return false;
    }

    if (!ExecSQL(db, "BEGIN TRANSACTION;")) { sqlite3_finalize(stmt); return false; }

    bool ok = true;
    for (int row = 0; ok && row < rows; ++row) {
        for (int col = 0; ok && col < cols; ++col) {
            double x, y;
            CellCenter(geotransform, col, row, x, y);
            size_t idx = static_cast<size_t>(row) * cols + col;

            sqlite3_reset(stmt);
            sqlite3_bind_int(stmt, 1, col);
            sqlite3_bind_int(stmt, 2, row);
            sqlite3_bind_double(stmt, 3, x);
            sqlite3_bind_double(stmt, 4, y);
            for (size_t k = 0; k < bands.size(); ++k) {
                sqlite3_bind_double(stmt, static_cast<int>(5 + k), (*bands[k].data)[idx]);
            }
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                std::cerr << "Error: SQLite insert failed at row " << row << ", col " << col << ": "
                          << sqlite3_errmsg(db) << "\n";
                ok = false;
            }
        }
    }

    sqlite3_finalize(stmt);
    if (!ok) {
        ExecSQL(db, "ROLLBACK;");
        return false;
    }
    return ExecSQL(db, "COMMIT;");
}

bool ConcatenateCSV(const std::filesystem::path& outputPath,
                     const std::vector<std::filesystem::path>& tileTablePaths) {
    std::ofstream out(outputPath);
    if (!out.is_open()) {
        std::cerr << "Error: could not open '" << outputPath.string() << "' for writing.\n";
        return false;
    }

    bool wroteHeader = false;
    for (const auto& tilePath : tileTablePaths) {
        std::ifstream in(tilePath);
        if (!in.is_open()) {
            std::cerr << "Warning: could not open per-tile table '" << tilePath.string() << "' -- skipped.\n";
            continue;
        }
        std::string line;
        bool isFirstLine = true;
        while (std::getline(in, line)) {
            if (isFirstLine) {
                isFirstLine = false;
                if (wroteHeader) continue; // every subsequent file's header line is dropped
                out << line << "\n";
                wroteHeader = true;
                continue;
            }
            out << line << "\n";
        }
    }
    return true;
}

bool ConcatenateSQLite(const std::filesystem::path& outputPath,
                       const std::vector<std::filesystem::path>& tileTablePaths,
                       const std::string& tableName = "grid") {
    if (tileTablePaths.empty()) {
        std::cerr << "Error: no per-tile tables to concatenate.\n";
        return false;
    }

    std::error_code ec;
    std::filesystem::remove(outputPath, ec);
    // Seed the merged database from the first tile -- gives it the correct
    // schema for free, matching whatever bands WriteGridTable wrote there,
    // without this function needing to know the column list itself.
    std::filesystem::copy_file(tileTablePaths[0], outputPath, ec);
    if (ec) {
        std::cerr << "Error: could not seed '" << outputPath.string() << "' from '"
                  << tileTablePaths[0].string() << "': " << ec.message() << "\n";
        return false;
    }

    sqlite3* rawDb = nullptr;
    if (sqlite3_open(outputPath.string().c_str(), &rawDb) != SQLITE_OK) {
        std::cerr << "Error: could not open merged SQLite database '" << outputPath.string() << "'.\n";
        if (rawDb) sqlite3_close(rawDb);
        return false;
    }
    SQLiteConnection conn(rawDb);
    sqlite3* db = conn.get();
    ApplyBulkInsertPragmas(db);

    bool ok = true;
    for (size_t i = 1; ok && i < tileTablePaths.size(); ++i) {
        sqlite3_stmt* attachStmt = nullptr;
        if (sqlite3_prepare_v2(db, "ATTACH DATABASE ? AS srcdb;", -1, &attachStmt, nullptr) != SQLITE_OK) {
            std::cerr << "Error: failed to prepare ATTACH: " << sqlite3_errmsg(db) << "\n";
            ok = false;
            break;
        }
        sqlite3_bind_text(attachStmt, 1, tileTablePaths[i].string().c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(attachStmt) != SQLITE_DONE) {
            std::cerr << "Error: ATTACH DATABASE failed for '" << tileTablePaths[i].string() << "': "
                      << sqlite3_errmsg(db) << "\n";
            ok = false;
        }
        sqlite3_finalize(attachStmt);
        if (!ok) break;

        ok = ExecSQL(db, "BEGIN TRANSACTION;") &&
             ExecSQL(db, "INSERT INTO " + tableName + " SELECT * FROM srcdb." + tableName + ";") &&
             ExecSQL(db, "COMMIT;");
        if (!ok) {
            ExecSQL(db, "ROLLBACK;");
        }
        ExecSQL(db, "DETACH DATABASE srcdb;");
    }

    return ok;
}

} // namespace

bool WriteGridTable(const std::filesystem::path& outputPath, int cols, int rows,
                     const double geotransform[6], const std::vector<BandDef>& bands,
                     float /*noDataValue*/) {
    if (!ValidateBandSizes(bands, static_cast<size_t>(cols) * rows)) return false;

    switch (DetectFormat(outputPath)) {
        case TableFormat::CSV:
            return WriteGridTableCSV(outputPath, cols, rows, geotransform, bands);
        case TableFormat::SQLite:
            return WriteGridTableSQLite(outputPath, cols, rows, geotransform, bands);
        default:
            std::cerr << "Error: unrecognized table output extension for '" << outputPath.string()
                      << "' -- expected .csv, .sqlite, .db, or .sqlite3.\n";
            return false;
    }
}

bool ConcatenateGridTables(const std::filesystem::path& outputPath,
                            const std::vector<std::filesystem::path>& tileTablePaths) {
    switch (DetectFormat(outputPath)) {
        case TableFormat::CSV:
            return ConcatenateCSV(outputPath, tileTablePaths);
        case TableFormat::SQLite:
            return ConcatenateSQLite(outputPath, tileTablePaths);
        default:
            std::cerr << "Error: unrecognized table output extension for '" << outputPath.string()
                      << "' -- expected .csv, .sqlite, .db, or .sqlite3.\n";
            return false;
    }
}

// ---------------------------------------------------------------------
// RowTableWriter
// ---------------------------------------------------------------------

struct RowTableWriter::Impl {
    TableFormat format{TableFormat::Unknown};
    size_t numColumns{0};

    // CSV state
    std::ofstream csv;

    // SQLite state
    sqlite3* db{nullptr};
    sqlite3_stmt* insertStmt{nullptr};
    bool inTransaction{false};

    ~Impl() {
        if (insertStmt) sqlite3_finalize(insertStmt);
        if (db) {
            if (inTransaction) sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
            sqlite3_close(db);
        }
    }
};

RowTableWriter::RowTableWriter() : m_impl(std::make_unique<Impl>()) {}
RowTableWriter::~RowTableWriter() = default;

bool RowTableWriter::Open(const std::filesystem::path& outputPath, const std::vector<std::string>& columnNames) {
    m_impl->format = DetectFormat(outputPath);
    m_impl->numColumns = columnNames.size();

    if (m_impl->format == TableFormat::CSV) {
        m_impl->csv.open(outputPath);
        if (!m_impl->csv.is_open()) {
            std::cerr << "Error: could not open '" << outputPath.string() << "' for writing.\n";
            return false;
        }
        for (size_t i = 0; i < columnNames.size(); ++i) {
            if (i > 0) m_impl->csv << ",";
            m_impl->csv << columnNames[i];
        }
        m_impl->csv << "\n";
        return true;
    }

    if (m_impl->format == TableFormat::SQLite) {
        std::error_code ec;
        std::filesystem::remove(outputPath, ec);
        if (sqlite3_open(outputPath.string().c_str(), &m_impl->db) != SQLITE_OK) {
            std::cerr << "Error: could not create SQLite database '" << outputPath.string() << "'.\n";
            return false;
        }
        ApplyBulkInsertPragmas(m_impl->db);

        std::string createSQL = "CREATE TABLE grid (";
        std::string insertSQL = "INSERT INTO grid VALUES (";
        for (size_t i = 0; i < columnNames.size(); ++i) {
            if (i > 0) { createSQL += ", "; insertSQL += ","; }
            createSQL += "\"" + columnNames[i] + "\" REAL";
            insertSQL += "?";
        }
        createSQL += ");";
        insertSQL += ");";

        if (!ExecSQL(m_impl->db, createSQL)) return false;
        if (sqlite3_prepare_v2(m_impl->db, insertSQL.c_str(), -1, &m_impl->insertStmt, nullptr) != SQLITE_OK) {
            std::cerr << "Error: failed to prepare SQLite insert statement: " << sqlite3_errmsg(m_impl->db) << "\n";
            return false;
        }
        m_impl->inTransaction = ExecSQL(m_impl->db, "BEGIN TRANSACTION;");
        return m_impl->inTransaction;
    }

    std::cerr << "Error: unrecognized table output extension for '" << outputPath.string()
              << "' -- expected .csv, .sqlite, .db, or .sqlite3.\n";
    return false;
}

bool RowTableWriter::WriteRow(const std::vector<double>& values) {
    if (values.size() != m_impl->numColumns) {
        std::cerr << "Error: RowTableWriter::WriteRow got " << values.size() << " values, expected "
                  << m_impl->numColumns << ".\n";
        return false;
    }

    if (m_impl->format == TableFormat::CSV) {
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) m_impl->csv << ",";
            if (std::isnan(values[i])) m_impl->csv << "NA";
            else m_impl->csv << values[i];
        }
        m_impl->csv << "\n";
        return true;
    }

    if (m_impl->format == TableFormat::SQLite) {
        sqlite3_reset(m_impl->insertStmt);
        for (size_t i = 0; i < values.size(); ++i) {
            if (std::isnan(values[i])) {
                sqlite3_bind_null(m_impl->insertStmt, static_cast<int>(i + 1));
            } else {
                sqlite3_bind_double(m_impl->insertStmt, static_cast<int>(i + 1), values[i]);
            }
        }
        if (sqlite3_step(m_impl->insertStmt) != SQLITE_DONE) {
            std::cerr << "Error: SQLite row insert failed: " << sqlite3_errmsg(m_impl->db) << "\n";
            return false;
        }
        return true;
    }

    return false;
}

bool RowTableWriter::Close() {
    if (m_impl->format == TableFormat::CSV) {
        m_impl->csv.close();
        return true;
    }
    if (m_impl->format == TableFormat::SQLite) {
        if (m_impl->insertStmt) {
            sqlite3_finalize(m_impl->insertStmt);
            m_impl->insertStmt = nullptr;
        }
        bool ok = true;
        if (m_impl->inTransaction) {
            ok = ExecSQL(m_impl->db, "COMMIT;");
            m_impl->inTransaction = false;
        }
        return ok;
    }
    return false;
}

} // namespace fusion::table
