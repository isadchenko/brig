// Andrew Naplavkov

#ifndef BRIG_DATABASE_DETAIL_SQL_REGISTER_RASTER_HPP
#define BRIG_DATABASE_DETAIL_SQL_REGISTER_RASTER_HPP

#include <algorithm>
#include <brig/database/column_definition.hpp>
#include <brig/database/command.hpp>
#include <brig/database/detail/get_schema.hpp>
#include <brig/database/detail/get_table_definition.hpp>
#include <brig/database/detail/normalize_identifier.hpp>
#include <brig/database/detail/sql_identifier.hpp>
#include <brig/database/detail/sql_create.hpp>
#include <brig/database/detail/sql_tables.hpp>
#include <brig/database/global.hpp>
#include <brig/database/identifier.hpp>
#include <brig/database/index_definition.hpp>
#include <brig/database/raster_pyramid.hpp>
#include <brig/database/table_definition.hpp>
#include <brig/database/variant.hpp>
#include <brig/string_cast.hpp>
#include <ios>
#include <locale>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

namespace brig { namespace database { namespace detail {

inline column_definition get_column_definition(const std::string& name, column_type type)
{
  column_definition col;
  col.name = name;
  col.type = type;
  col.not_null = true;
  return col;
}

inline table_definition get_simple_rasters_definition(DBMS sys)
{
  table_definition tbl;
  tbl.id.name = "simple_rasters";
  normalize_identifier(sys, tbl.id);

  tbl.indexes.push_back(index_definition());
  index_definition& idx(tbl.indexes.back());
  idx.type = Primary;

  if (SQLite != sys)  { tbl.columns.push_back( get_column_definition("schema", String) ); idx.columns.push_back("schema"); }
  tbl.columns.push_back( get_column_definition("table", String) ); idx.columns.push_back("table");
  tbl.columns.push_back( get_column_definition("raster", String) ); idx.columns.push_back("raster");

  if (SQLite != sys) tbl.columns.push_back( get_column_definition("base_schema", String) );
  tbl.columns.push_back( get_column_definition("base_table", String) );
  tbl.columns.push_back( get_column_definition("base_raster", String) );

  tbl.columns.push_back( get_column_definition("geometry", String) );
  tbl.columns.push_back( get_column_definition("resolution_x", Double) );
  tbl.columns.push_back( get_column_definition("resolution_y", Double) );

  return tbl;
}

inline void sql_register_raster(std::shared_ptr<command> cmd, raster_pyramid& raster, std::vector<std::string>& sql)
{
  const DBMS sys(cmd->system());
  const std::string schema(get_schema(cmd));

  identifier simple_rasters;
  simple_rasters.name = "simple_rasters";
  normalize_identifier(sys, simple_rasters);

  cmd->exec(sql_tables(sys, simple_rasters.name));
  std::vector<variant> row;
  
  if (cmd->fetch(row))
  {
    simple_rasters.schema = string_cast<char>(row[0]);
    if (cmd->fetch(row)) throw std::runtime_error("ambiguous simple_rasters error");
  }
  else
  {
    std::vector<std::string> strs;
    sql_create(sys, get_simple_rasters_definition(sys), strs);
    for (auto str(std::begin(strs)); str != std::end(strs); ++str)
      cmd->exec(*str);
    simple_rasters.schema = schema;
  }
  auto tbl(get_table_definition(cmd, simple_rasters));

  std::vector<std::string> reg;
  for (auto lvl(std::begin(raster.levels)); lvl != std::end(raster.levels); ++lvl)
  {
    lvl->geometry_layer.schema = schema;
    normalize_identifier(sys, lvl->geometry_layer);

    std::ostringstream stream; stream.imbue(std::locale::classic()); stream << std::scientific; stream.precision(17);
    stream << "INSERT INTO " << sql_identifier(sys, tbl.id) << "(";
    for (size_t col(0); col < tbl.columns.size(); ++col)
    {
      if (col > 0) stream << ", ";
      stream << sql_identifier(sys, tbl.columns[col].name);
    }
    stream << ") VALUES (";
    for (size_t col(0); col < tbl.columns.size(); ++col)
    {
      if (col > 0) stream << ", ";
      if ("schema" == tbl.columns[col].name) stream << "'" << schema << "'";
      else if ("table" == tbl.columns[col].name) stream << "'" << lvl->geometry_layer.name << "'";
      else if ("raster" == tbl.columns[col].name)
      {
        if (typeid(column_definition) == lvl->raster_column.type()) lvl->raster_column = ::boost::get<column_definition>(lvl->raster_column).name;
        stream << "'" << ::boost::get<std::string>(lvl->raster_column) << "'";
      }
      else if ("base_schema" == tbl.columns[col].name) stream << "'" << schema << "'";
      else if ("base_table" == tbl.columns[col].name) stream << "'" << raster.id.name << "'";
      else if ("base_raster" == tbl.columns[col].name) stream << "'" << raster.id.qualifier << "'";
      else if ("geometry" == tbl.columns[col].name) stream << "'" << lvl->geometry_layer.qualifier << "'";
      else if ("resolution_x" == tbl.columns[col].name) stream << lvl->resolution.get<0>();
      else if ("resolution_y" == tbl.columns[col].name) stream << lvl->resolution.get<1>();
    }
    stream << ")";
    reg.push_back(stream.str());
  }

  if (raster.levels.empty()) throw std::runtime_error("raster error");
  raster.id = raster.levels.front().geometry_layer;
  raster.id.qualifier = ::boost::get<std::string>(raster.levels.front().raster_column);

  std::string unreg;
  unreg += "DELETE FROM " + sql_identifier(sys, simple_rasters) + " WHERE ";
  if (SQLite != sys) unreg += sql_identifier(sys, "base_schema") + " = '" + raster.id.schema + "' AND ";
  unreg += sql_identifier(sys, "base_table") + " = '" + raster.id.name + "' AND " + sql_identifier(sys, "base_raster") + " = '" + raster.id.qualifier + "'";

  sql.push_back(unreg);
  sql.insert(std::end(sql), std::begin(reg), std::end(reg));
}

} } } // brig::database::detail

#endif // BRIG_DATABASE_DETAIL_REGISTER_RASTER_HPP