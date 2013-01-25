// Andrew Naplavkov

#ifndef BRIG_DATABASE_ODBC_DETAIL_BINDING_FACTORY_HPP
#define BRIG_DATABASE_ODBC_DETAIL_BINDING_FACTORY_HPP

#include <boost/variant/apply_visitor.hpp>
#include <boost/variant/static_visitor.hpp>
#include <brig/blob_t.hpp>
#include <brig/column_def.hpp>
#include <brig/database/odbc/detail/binding.hpp>
#include <brig/database/odbc/detail/binding_blob.hpp>
#include <brig/database/odbc/detail/binding_impl.hpp>
#include <brig/database/odbc/detail/binding_null.hpp>
#include <brig/database/odbc/detail/binding_string.hpp>
#include <brig/database/odbc/detail/lib.hpp>
#include <brig/global.hpp>
#include <brig/null_t.hpp>
#include <brig/variant.hpp>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace brig { namespace database { namespace odbc { namespace detail {

class binding_visitor : public ::boost::static_visitor<binding*> {
  const DBMS m_sys;
  const column_def& m_param;

  SQLSMALLINT c_type() const;
  SQLSMALLINT sql_type() const;

public:
  explicit binding_visitor(DBMS sys, const column_def& param) : m_sys(sys), m_param(param)  {}
  binding* operator()(const null_t&) const  { return new binding_null(c_type(), sql_type()); }
  binding* operator()(int16_t v) const  { return new binding_impl<int16_t, SQL_C_SSHORT, SQL_SMALLINT>(v); }
  binding* operator()(int32_t v) const  { return new binding_impl<int32_t, SQL_C_SLONG, SQL_INTEGER>(v); }
  binding* operator()(int64_t v) const  { return Postgres == m_sys? operator()(int32_t(v)): new binding_impl<int64_t, SQL_C_SBIGINT, SQL_BIGINT>(v); } // todo:
  binding* operator()(float v) const  { return new binding_impl<float, SQL_C_FLOAT, SQL_REAL>(v); }
  binding* operator()(double v) const  { return new binding_impl<double, SQL_C_DOUBLE, SQL_DOUBLE>(v); }
  binding* operator()(const std::string& r) const  { return new binding_string(sql_type(), r); }
  binding* operator()(const blob_t& r) const  { return new binding_blob(sql_type(), r); }
}; // binding_visitor

inline SQLSMALLINT binding_visitor::c_type() const
{
  switch (m_param.type)
  {
    default: throw std::runtime_error("ODBC type error");
    case Blob:
    case Geometry: return SQL_C_BINARY;
    case Double: return SQL_C_DOUBLE;
    case Integer: return SQL_C_SBIGINT;
    case String: return SQL_C_WCHAR;
  };
}

inline SQLSMALLINT binding_visitor::sql_type() const
{
  switch (m_param.type)
  {
    default: throw std::runtime_error("ODBC type error");
    case Blob:
      switch (m_sys)
      {
      default: return SQL_VARBINARY;
      case MS_SQL:
      case Ingres: return SQL_LONGVARBINARY;
      }
      break;
    case Geometry:
      switch (m_sys)
      {
      default: return SQL_VARBINARY;
      case MS_SQL:
      case Ingres: return SQL_LONGVARBINARY;
      case Informix: return SQL_INFX_UDT_LVARCHAR;
      }
      break;
    case Double: return SQL_DOUBLE;
    case Integer: return SQL_BIGINT;
    case String:
      switch (m_sys)
      {
      default: return SQL_WVARCHAR;
      case MS_SQL: return SQL_WLONGVARCHAR;
      }
      break;
  };
} // binding_visitor::

inline binding* binding_factory(DBMS sys, const column_def& param)
{
  return ::boost::apply_visitor(binding_visitor(sys, param), param.query_value);
}

} } } } // brig::database::odbc::detail

#endif // BRIG_DATABASE_ODBC_DETAIL_BINDING_FACTORY_HPP
