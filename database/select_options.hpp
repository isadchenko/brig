// Andrew Naplavkov

#ifndef BRIG_DATABASE_SELECT_OPTIONS_HPP
#define BRIG_DATABASE_SELECT_OPTIONS_HPP

#include <brig/boost/geometry.hpp>
#include <brig/database/column_detail.hpp>
#include <brig/database/variant.hpp>
#include <string>
#include <vector>

namespace brig { namespace database {

struct select_options {
  std::string geometry_column;
  brig::boost::box box;
  std::string sql_filter;
  std::vector<variant> parameters;
  std::vector<column_detail> expression_columns;
  std::vector<std::string> select_columns;
  int rows;

  select_options() : box(brig::boost::point(0, 0), brig::boost::point(0, 0)), rows(-1)  {}
}; // select_options

} } // brig::database

#endif // BRIG_DATABASE_SELECT_OPTIONS_HPP
