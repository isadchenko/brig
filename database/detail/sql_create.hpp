// Andrew Naplavkov

#ifndef BRIG_DATABASE_DETAIL_SQL_CREATE_HPP
#define BRIG_DATABASE_DETAIL_SQL_CREATE_HPP

#include <algorithm>
#include <brig/blob_t.hpp>
#include <brig/boost/envelope.hpp>
#include <brig/boost/geom_from_wkb.hpp>
#include <brig/database/column_definition.hpp>
#include <brig/database/detail/normalize_identifier.hpp>
#include <brig/database/detail/sql_identifier.hpp>
#include <brig/database/global.hpp>
#include <brig/database/identifier.hpp>
#include <brig/database/index_definition.hpp>
#include <brig/database/table_definition.hpp>
#include <brig/string_cast.hpp>
#include <locale>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

namespace brig { namespace database { namespace detail {

inline void sql_create(DBMS sys, table_definition tbl, std::vector<std::string>& sql)
{
  using namespace brig::boost;
  static const int CharsLimit = 250;
  auto loc = std::locale::classic();

  // unknown column type
  if (VoidSystem == sys) throw std::runtime_error("SQL error");
  auto col_end( std::remove_if(std::begin(tbl.columns), std::end(tbl.columns), [](const column_definition& c){ return VoidColumn == c.type; }) );
  for (auto idx(std::begin(tbl.indexes)); idx != std::end(tbl.indexes); ++idx)
    for (auto col_name(std::begin(idx->columns)); col_name != std::end(idx->columns); ++col_name)
      if (std::find_if(std::begin(tbl.columns), col_end, [&](const column_definition& c){ return c.name == *col_name; }) == col_end)
        idx->type = VoidIndex;
  auto idx_end( std::remove_if(std::begin(tbl.indexes), std::end(tbl.indexes), [](const index_definition& i){ return VoidIndex == i.type; }) );
  normalize_identifier(sys, tbl);

  // columns, primary key
  {

  std::ostringstream stream; stream.imbue(loc);
  stream << "CREATE TABLE " << sql_identifier(sys, tbl.name) << " (";
  bool first(true);

  // force primary key if necessary
  switch (sys)
  {
  default: break;
  case MS_SQL:
    if (std::find_if(std::begin(tbl.indexes), idx_end, [&](const index_definition& i){ return Primary == i.type; }) == idx_end)
    {
      auto idx(std::find_if(std::begin(tbl.indexes), idx_end, [&](const index_definition& i){ return Unique == i.type; }));
      if (idx == idx_end)
      {
        stream << "ID BIGINT IDENTITY PRIMARY KEY";
        first = false;
      }
      else
        idx->type = Primary;
    }
    break;
  case Oracle:
    if (std::find_if(std::begin(tbl.indexes), idx_end, [&](const index_definition& i){ return Primary == i.type || Unique == i.type; }) == idx_end)
    {
      stream << "ID NVARCHAR2(32) DEFAULT SYS_GUID() PRIMARY KEY";
      first = false;
    }
    break;
  }

  for (auto col(std::begin(tbl.columns)); col != col_end; ++col)
  {
    if (Geometry == col->type)
      switch (sys)
      {
      default: break;
      case Postgres:
      case SQLite: continue;
      case Oracle:
        // The TABLE_NAME and COLUMN_NAME values are always converted to uppercase when you insert them into the USER_SDO_GEOM_METADATA view
        for (auto idx(std::begin(tbl.indexes)); idx != idx_end; ++idx)
          for (auto col_name(std::begin(idx->columns)); col_name != std::end(idx->columns); ++col_name)
            if (*col_name == col->name)
              normalize_identifier(sys, *col_name);
        normalize_identifier(sys, col->name);
        break; 
      }

    if (first) first = false;
    else stream << ", ";
    stream << sql_identifier(sys, col->name) << " ";
    int chars(CharsLimit);
    if (col->chars > 0 && col->chars < CharsLimit) chars = col->chars;

    switch (sys)
    {
    case VoidSystem: throw std::runtime_error("SQL error");

    case CUBRID:
      switch (col->type)
      {
      case VoidColumn:
      case Geometry: throw std::runtime_error("SQL error");
      case Blob: stream << "BIT VARYING"; break;
      case Double: stream << "DOUBLE"; break;
      case Integer: stream << "BIGINT"; break;
      case String: stream << "STRING"; break;
      }
      break;

    case DB2:
      switch (col->type)
      {
      case VoidColumn: throw std::runtime_error("SQL error");
      case Blob: stream << "BLOB"; break;
      case Double: stream << "DOUBLE"; break;
      case Geometry: stream << "DB2GSE.ST_GEOMETRY"; break;
      case Integer: stream << "BIGINT"; break;
      case String: stream << "VARGRAPHIC(" << chars << ")"; break;
      }
      // When UNIQUE is used, null values are treated as any other values. For example, if the key is a single column that may contain null values, that column may contain no more than one null value.
      for (auto idx(std::begin(tbl.indexes)); idx != idx_end; ++idx)
        if (Primary == idx->type || Unique == idx->type)
          for (auto col_name(std::begin(idx->columns)); col_name != std::end(idx->columns); ++col_name)
            if (*col_name == col->name)
              col->not_null = true;
      break;

    case MS_SQL:
      switch (col->type)
      {
      case VoidColumn: throw std::runtime_error("SQL error");
      case Blob: stream << "VARBINARY(MAX)"; break;
      case Double: stream << "FLOAT"; break;
      case Geometry: stream << "GEOMETRY"; break;
      case Integer: stream << "BIGINT"; break;
      case String: stream << "NVARCHAR(" << chars << ")"; break;
      }
      break;

    case MySQL:
      switch (col->type)
      {
      case VoidColumn: throw std::runtime_error("SQL error");
      case Blob: stream << "LONGBLOB"; break;
      case Double: stream << "DOUBLE"; break;
      case Geometry:
        stream << "GEOMETRY";
        // columns in spatial indexes must be declared NOT NULL
        for (auto idx(std::begin(tbl.indexes)); idx != idx_end; ++idx)
          if (Spatial == idx->type)
          {
            if (1 != idx->columns.size()) throw std::runtime_error("table error");
            if (idx->columns.front() == col->name) col->not_null = true;
          }
        break;
      case Integer: stream << "BIGINT"; break;
      case String: stream << "NVARCHAR(" << chars << ")"; break;
      }
      break;

    case Oracle:
      switch (col->type)
      {
      case VoidColumn: throw std::runtime_error("SQL error");
      case Blob: stream << "BLOB"; break;
      case Double: stream << "BINARY_DOUBLE"; break;
      case Geometry: stream << "MDSYS.SDO_GEOMETRY"; break;
      case Integer: stream << "NUMBER(19)"; break;
      case String: stream << "NVARCHAR2(" << chars << ")"; break;
      }
      break;

    case Postgres:
      switch (col->type)
      {
      case VoidColumn:
      case Geometry: throw std::runtime_error("SQL error");
      case Blob: stream << "BYTEA"; break;
      case Double: stream << "DOUBLE PRECISION"; break;
      case Integer: stream << "BIGINT"; break;
      case String: stream << "VARCHAR(" << chars << ")"; break;
      }
      break;

    case SQLite:
      switch (col->type)
      {
      case VoidColumn:
      case Geometry: throw std::runtime_error("SQL error");
      case Blob: stream << "BLOB"; break;
      case Double: stream << "REAL"; break; // real affinity
      case Integer: stream << "INTEGER"; break; // integer affinity
      case String: stream << "TEXT"; break; // text affinity
      }
      break;
    }

    if (col->not_null) stream << " NOT NULL";
  }

  // primary key
  auto idx(std::find_if(std::begin(tbl.indexes), idx_end, [&](const index_definition& i){ return Primary == i.type; }));
  if (idx != idx_end)
  {
    idx->schema = "";
    idx->name = "";
    idx->qualifier = "";
    stream << ", PRIMARY KEY (";
    bool first(true);
    for (auto col_name(std::begin(idx->columns)); col_name != std::end(idx->columns); ++col_name)
    {
      if (first) first = false;
      else stream << ", ";
      stream << sql_identifier(sys, *col_name);
    }
    stream << ")";
  }
  stream << ")";
  if (MySQL == sys) stream << " ENGINE = MyISAM";
  sql.push_back(stream.str());

  } // columns, primary key

  // geometry
  for (auto col(std::begin(tbl.columns)); col != col_end; ++col)
    if (Geometry == col->type)
    {
      std::ostringstream stream; stream.imbue(loc);
      switch (sys)
      {
      case VoidSystem:
      case CUBRID: throw std::runtime_error("SQL error");
      case DB2: stream << "BEGIN ATOMIC DECLARE msg_code INTEGER; DECLARE msg_text VARCHAR(1024); call DB2GSE.ST_register_spatial_column(NULL, '" << sql_identifier(sys, tbl.name) << "', '" << sql_identifier(sys, col->name) << "', (SELECT SRS_NAME FROM DB2GSE.ST_SPATIAL_REFERENCE_SYSTEMS WHERE ORGANIZATION LIKE 'EPSG' AND ORGANIZATION_COORDSYS_ID = " << col->epsg << " ORDER BY SRS_ID FETCH FIRST 1 ROWS ONLY), msg_code, msg_text); END"; break;
      case MS_SQL:
      case MySQL: break;
      case Oracle:
        {
        if (typeid(blob_t) != col->query_condition.type()) throw std::runtime_error("SQL error");
        const blob_t& blob(::boost::get<blob_t>(col->query_condition));
        if (blob.empty()) throw std::runtime_error("SQL error");
        auto box(envelope(geom_from_wkb(blob)));
        const double xmin(box.min_corner().get<0>()), ymin(box.min_corner().get<1>()), xmax(box.max_corner().get<0>()), ymax(box.max_corner().get<1>()), eps(0.000001);
        stream << "BEGIN DELETE FROM MDSYS.USER_SDO_GEOM_METADATA WHERE TABLE_NAME = '" << tbl.name << "' AND COLUMN_NAME = '" << col->name << "'; INSERT INTO MDSYS.USER_SDO_GEOM_METADATA (TABLE_NAME, COLUMN_NAME, DIMINFO, SRID) VALUES ('" << tbl.name << "', '" << col->name << "', MDSYS.SDO_DIM_ARRAY(MDSYS.SDO_DIM_ELEMENT('X', " << xmin << ", " << xmax << ", " << eps << "), MDSYS.SDO_DIM_ELEMENT('Y', " << ymin << ", " << ymax << ", " << eps << ")), (SELECT SRID FROM MDSYS.SDO_COORD_REF_SYS WHERE DATA_SOURCE LIKE 'EPSG' AND SRID = " << col->epsg << " AND ROWNUM <= 1)); END;";
        }
        break;
      case Postgres: stream << "SELECT AddGeometryColumn('" << tbl.name << "', '" << col->name << "', (SELECT SRID FROM PUBLIC.SPATIAL_REF_SYS WHERE AUTH_NAME LIKE 'EPSG' AND AUTH_SRID = " << col->epsg << " ORDER BY SRID FETCH FIRST 1 ROWS ONLY), 'GEOMETRY', 2)"; break;
      case SQLite: stream << "SELECT AddGeometryColumn('" << tbl.name << "', '" << col->name << "', (SELECT SRID FROM SPATIAL_REF_SYS WHERE AUTH_NAME LIKE 'EPSG' AND AUTH_SRID = " << col->epsg << " ORDER BY SRID LIMIT 1), 'GEOMETRY', 2)"; break;
      }
      const std::string str(stream.str()); if (!str.empty()) sql.push_back(str);
    }

  // indexes
  size_t counter(0);
  for (auto idx(std::begin(tbl.indexes)); idx != idx_end; ++idx)
  {
    if (Primary == idx->type) continue;
    if (idx->columns.empty()) throw std::runtime_error("table error");
    idx->name = tbl.name + "_idx_" + string_cast<char>(++counter);

    std::ostringstream stream; stream.imbue(loc);
    if (Spatial == idx->type)
    {
      if (1 != idx->columns.size()) throw std::runtime_error("table error");
      switch (sys)
      {
      case VoidSystem:
      case CUBRID: throw std::runtime_error("SQL error");
      case DB2: stream << "CREATE INDEX " << sql_identifier(sys, idx->name) << " ON " << sql_identifier(sys, tbl.name) << " (" << sql_identifier(sys, idx->columns.front()) << ") EXTEND USING DB2GSE.SPATIAL_INDEX (1, 0, 0)"; break;
      case MS_SQL:
        {
        auto col(std::find_if(std::begin(tbl.columns), col_end, [&](const column_definition& c){ return c.name == idx->columns.front(); }));
        if (typeid(blob_t) != col->query_condition.type()) throw std::runtime_error("SQL error");
        const blob_t& blob(::boost::get<blob_t>(col->query_condition));
        if (blob.empty()) throw std::runtime_error("SQL error");
        auto box(envelope(geom_from_wkb(blob)));
        const double xmin(box.min_corner().get<0>()), ymin(box.min_corner().get<1>()), xmax(box.max_corner().get<0>()), ymax(box.max_corner().get<1>());
        stream << "CREATE SPATIAL INDEX " << sql_identifier(sys, idx->name) << " ON " << sql_identifier(sys, tbl.name) << " (" << sql_identifier(sys, idx->columns.front()) << ") USING GEOMETRY_GRID WITH (BOUNDING_BOX = (" << xmin << ", " << ymin << ", " << xmax << ", " << ymax << "))";
        }
        break;
      case MySQL: stream << "CREATE SPATIAL INDEX " << sql_identifier(sys, idx->name) << " ON " << sql_identifier(sys, tbl.name) << " (" << sql_identifier(sys, idx->columns.front()) << ")"; break;
      case Oracle: stream << "CREATE INDEX " << sql_identifier(sys, idx->name) << " ON " << sql_identifier(sys, tbl.name) << " (" << sql_identifier(sys, idx->columns.front()) << ") INDEXTYPE IS MDSYS.SPATIAL_INDEX"; break;
      case Postgres: stream << "CREATE INDEX " << sql_identifier(sys, idx->name) << " ON " << sql_identifier(sys, tbl.name) << " USING GIST(" << sql_identifier(sys, idx->columns.front()) << ")"; break;
      case SQLite: stream << "SELECT CreateSpatialIndex('" << tbl.name << "', '" << idx->columns.front() << "')"; idx->name = ""; break;
      }
    }
    else
    {
      stream << "CREATE ";
      if (Unique == idx->type) stream << "UNIQUE ";
      stream << "INDEX " << sql_identifier(sys, idx->name) << " ON " << sql_identifier(sys, tbl.name) << " (";
      bool first(true);
      for (auto col_name(std::begin(idx->columns)); col_name != std::end(idx->columns); ++col_name)
      {
        if (first) first = false;
        else stream << ", ";
        stream << sql_identifier(sys, *col_name);
      }
      stream << ")";
    }
    sql.push_back(stream.str());
  }
}

} } } // brig::database::detail

#endif // BRIG_DATABASE_DETAIL_SQL_CREATE_HPP
