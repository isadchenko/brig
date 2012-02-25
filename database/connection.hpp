// Andrew Naplavkov

#ifndef BRIG_DATABASE_CONNECTION_HPP
#define BRIG_DATABASE_CONNECTION_HPP

#include <algorithm>
#include <brig/boost/geometry.hpp>
#include <brig/database/column.hpp>
#include <brig/database/column_abstract.hpp>
#include <brig/database/column_detail.hpp>
#include <brig/database/command.hpp>
#include <brig/database/command_allocator.hpp>
#include <brig/database/detail/deleter.hpp>
#include <brig/database/detail/get_type.hpp>
#include <brig/database/detail/pool.hpp>
#include <brig/database/detail/sql_columns.hpp>
#include <brig/database/detail/sql_create.hpp>
#include <brig/database/detail/sql_drop.hpp>
#include <brig/database/detail/sql_geometry_layer.hpp>
#include <brig/database/detail/sql_geometry_layers.hpp>
#include <brig/database/detail/sql_indexed_columns.hpp>
#include <brig/database/detail/sql_insert.hpp>
#include <brig/database/detail/sql_mbr.hpp>
#include <brig/database/detail/sql_srid.hpp>
#include <brig/database/detail/sql_table.hpp>
#include <brig/database/detail/sql_tables.hpp>
#include <brig/database/detail/sqlite_table_detail.hpp>
#include <brig/database/global.hpp>
#include <brig/database/index_detail.hpp>
#include <brig/database/numeric_cast.hpp>
#include <brig/database/object.hpp>
#include <brig/database/rowset.hpp>
#include <brig/database/table_detail.hpp>
#include <brig/database/variant.hpp>
#include <brig/detail/string_cast.hpp>
#include <brig/unicode/lower_case.hpp>
#include <brig/unicode/transform.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace brig { namespace database {

template <bool Threading>
class connection {
  typedef detail::pool<Threading> pool_type;
  std::shared_ptr<pool_type> m_pool;

public:
  explicit connection(std::shared_ptr<command_allocator> allocator) : m_pool(new pool_type(allocator))  {}

  // metadata
  std::vector<object> get_tables();
  std::vector<column> get_geometry_layers();
  table_detail<column_detail> get_table_detail(const object& tbl);
  table_detail<column_abstract> get_table_abstract(const table_detail<column_detail>& tbl);
  brig::boost::box get_mbr(const object& tbl, const column_detail& col);

  // data
  std::shared_ptr<rowset> get_table
    ( const table_detail<column_detail>& tbl
    , const std::vector<std::string>& cols = std::vector<std::string>(), int rows = -1
    );
  std::shared_ptr<rowset> get_geometry_layer
    ( const table_detail<column_detail>& tbl, const std::string& lr, const brig::boost::box& box
    , const std::vector<std::string>& cols = std::vector<std::string>(), int rows = -1
    );

  // sql
  std::shared_ptr<command> get_command()  { return std::shared_ptr<command>(m_pool->allocate(), detail::deleter<pool_type>(m_pool)); }
  void before_create(table_detail<column_abstract>& tbl);
  std::vector<std::string> sql_create(table_detail<column_abstract>& tbl)  { return detail::sql_create(get_command()->system(), tbl); }
  std::vector<std::string> sql_drop(const table_detail<column_detail>& tbl)  { return detail::sql_drop(get_command()->system(), tbl); }
  std::string sql_insert(const table_detail<column_detail>& tbl, const std::vector<std::string>& cols = std::vector<std::string>());
}; // connection

template <bool Threading>
std::vector<object> connection<Threading>::get_tables()
{
  using namespace brig::database::detail;
  using namespace brig::detail;

  std::vector<object> res;
  auto cmd = get_command();
  cmd->exec(sql_tables(cmd->system()));
  std::vector<variant> row;
  while (cmd->fetch(row))
  {
    object tbl;
    tbl.schema = string_cast<char>(row[0]);
    tbl.name = string_cast<char>(row[1]);
    res.push_back(tbl);
  }
  return std::move(res);
}

template <bool Threading>
std::vector<column> connection<Threading>::get_geometry_layers()
{
  using namespace brig::database::detail;
  using namespace brig::detail;

  auto cmd = get_command();
  const DBMS sys(cmd->system());
  std::vector<variant> row;
  if (SQLite == sys)
  {
    cmd->exec(sql_tables(sys, "GEOMETRY_COLUMNS"));
    if (!cmd->fetch(row)) return std::vector<column>();
  }

  std::vector<column> res;
  cmd->exec(sql_geometry_layers(sys));
  while (cmd->fetch(row))
  {
    column col;
    col.table.schema = string_cast<char>(row[0]);
    col.table.name = string_cast<char>(row[1]);
    col.name = string_cast<char>(row[2]);
    res.push_back(col);
  }
  return std::move(res);
}

template <bool Threading>
table_detail<column_detail> connection<Threading>::get_table_detail(const object& tbl)
{
  using namespace brig::database::detail;
  using namespace brig::detail;
  using namespace brig::unicode;

  auto cmd = get_command();
  const DBMS sys(cmd->system());
  if (SQLite == sys) return sqlite_table_detail(cmd, tbl);

  // columns
  table_detail<column_detail> res;
  res.table = tbl;
  cmd->exec(sql_columns(sys, tbl));
  std::vector<variant> row;
  while (cmd->fetch(row))
  {
    column_detail col;
    col.name = string_cast<char>(row[0]);
    col.type.schema = string_cast<char>(row[1]);
    col.type.name = string_cast<char>(row[2]); 
    col.lower_case_type.schema = transform<std::string>(col.type.schema, lower_case);
    col.lower_case_type.name = transform<std::string>(col.type.name, lower_case);
    numeric_cast(row[3], col.chars);
    numeric_cast(row[4], col.precision);
    numeric_cast(row[5], col.scale);
    res.columns.push_back(std::move(col));
  }

  // indexes
  cmd->exec(sql_indexed_columns(sys, tbl));
  index_detail idx;
  while (cmd->fetch(row))
  {
    object index;
    index.schema = string_cast<char>(row[0]);
    index.name = string_cast<char>(row[1]);

    if (index.schema != idx.index.schema || index.name != idx.index.name)
    {
      if (VoidIndex != idx.type) res.indexes.push_back(std::move(idx));

      idx = index_detail();
      idx.index = index;
      int primary(0), unique(0), spatial(0);
      numeric_cast(row[2], primary);
      numeric_cast(row[3], unique);
      numeric_cast(row[4], spatial);
      if (primary) idx.type = Primary;
      else if (unique) idx.type = Unique;
      else if (spatial) idx.type = Spatial;
      else idx.type = Duplicate;
    }

    const std::string col_name(string_cast<char>(row[5]));
    idx.columns.push_back(col_name);
    if (std::find_if(res.columns.begin(), res.columns.end(), [&](const column_detail& col){ return col.name == col_name; }) == res.columns.end()) idx.type = VoidIndex; // expression

    int desc(0);
    if (numeric_cast(row[6], desc) && desc) idx.type = VoidIndex; // descending
  }
  if (VoidIndex != idx.type) res.indexes.push_back(std::move(idx));

  // srid, epsg, type detail
  for (auto p_col = res.columns.begin(); p_col != res.columns.end(); ++p_col)
  {
    const std::string sql(sql_srid(sys, tbl, *p_col));
    if (!sql.empty())
    {
      cmd->exec(sql);
      if (cmd->fetch(row))
      {
        numeric_cast(row[0], p_col->srid);
        if (row.size() > 1) numeric_cast(row[1], p_col->epsg);
        else p_col->epsg = p_col->srid;
        if (row.size() > 2)
        {
          p_col->type_detail = string_cast<char>(row[2]);
          p_col->lower_case_type_detail = transform<std::string>(p_col->type_detail, lower_case);
        }
      }
    }
  }
  return std::move(res);
}

template <bool Threading>
table_detail<column_abstract> connection<Threading>::get_table_abstract(const table_detail<column_detail>& tbl)
{
  auto cmd = get_command();
  const DBMS sys(cmd->system());
  table_detail<column_abstract> res;
  res.table.name = tbl.table.name;

  for (auto p_col = tbl.columns.begin(); p_col != tbl.columns.end(); ++p_col)
  {
    column_abstract col;
    col.name = p_col->name;
    col.type = detail::get_type(sys, *p_col);
    col.epsg = p_col->epsg;
    if (VoidColumn != col.type) res.columns.push_back(col);
  }

  for (auto p_idx = tbl.indexes.begin(); p_idx != tbl.indexes.end(); ++p_idx)
  {
    bool valid((Spatial == p_idx->type && 1 == p_idx->columns.size()) || (VoidIndex != p_idx->type && !p_idx->columns.empty()));

    for (auto p_col_name = p_idx->columns.begin(); p_col_name != p_idx->columns.end() && valid; ++p_col_name)
    {
      auto p_col = std::find_if(res.columns.begin(), res.columns.end(), [&](const column_abstract& col){ return *p_col_name == col.name; });
      if (p_col == res.columns.end() || (Spatial == p_idx->type && Geometry != p_col->type)) valid = false;
    }

    if (valid)
    {
      res.indexes.push_back(*p_idx);
      res.indexes.back().index.schema = "";
    }
  }
  return std::move(res);
}

template <bool Threading>
brig::boost::box connection<Threading>::get_mbr(const object& tbl, const column_detail& col)
{
  using namespace brig::boost;

  auto cmd = get_command();
  const std::string sql(detail::sql_mbr(cmd->system(), tbl, col));
  if (sql.empty())
    return box(point(-180, -90), point(180, 90)); // geodetic

  cmd->exec(sql);
  std::vector<variant> row;
  double xmin(0), ymin(0), xmax(0), ymax(0);
  if ( cmd->fetch(row)
    && numeric_cast(row[0], xmin)
    && numeric_cast(row[1], ymin)
    && numeric_cast(row[2], xmax)
    && numeric_cast(row[3], ymax)
     )
    return box(point(xmin, ymin), point(xmax, ymax));

  throw std::runtime_error("mbr error");
}

template <bool Threading>
std::shared_ptr<rowset> connection<Threading>::get_table(const table_detail<column_detail>& tbl, const std::vector<std::string>& cols, int rows)
{
  auto cmd = get_command();
  cmd->exec(detail::sql_table(cmd.get(), tbl, cols, rows));
  return cmd;
}

template <bool Threading>
std::shared_ptr<rowset> connection<Threading>::get_geometry_layer
  ( const table_detail<column_detail>& tbl, const std::string& lr, const brig::boost::box& box
  , const std::vector<std::string>& cols, int rows
  )
{
  auto cmd = get_command();
  cmd->exec(detail::sql_geometry_layer(cmd.get(), tbl, lr, box, cols, rows));
  return cmd;
}

template <bool Threading>
void connection<Threading>::before_create(table_detail<column_abstract>& tbl)
{
  auto cmd = get_command();
  const DBMS sys(cmd->system());

  for (auto p_col = tbl.columns.begin(); p_col != tbl.columns.end(); ++p_col)
    if (Geometry == p_col->type && Oracle == sys && typeid(bool) == p_col->mbr_need.type()) p_col->mbr_need = true;

  for (auto p_idx = tbl.indexes.begin(); p_idx != tbl.indexes.end(); ++p_idx)
    if (Spatial == p_idx->type && MS_SQL == sys)
    {
      auto p_col = std::find_if(tbl.columns.begin(), tbl.columns.end(), [&](const column_abstract& col){ return col.name == p_idx->columns.front(); });
      if (typeid(bool) == p_col->mbr_need.type()) p_col->mbr_need = true;
    }
}

template <bool Threading>
std::string connection<Threading>::sql_insert(const table_detail<column_detail>& tbl, const std::vector<std::string>& cols)
{
  auto cmd = get_command();
  return detail::sql_insert(cmd.get(), tbl, cols);
} // connection::

} } // brig::database

#endif // BRIG_DATABASE_CONNECTION_HPP
