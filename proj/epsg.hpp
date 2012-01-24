// Andrew Naplavkov

// http://www.epsg.org/

#ifndef BRIG_PROJ_EPSG_HPP
#define BRIG_PROJ_EPSG_HPP

#include <brig/proj/detail/lib.hpp>
#include <cstdint>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace brig { namespace proj {

class epsg
{
  struct resource
  {
    projPJ pj;
    resource(projPJ pj_) : pj(pj_)  {}
    ~resource()  { detail::lib::singleton().p_pj_free(pj); }
  }; // resource

  std::shared_ptr<resource> m_res;
  int32_t m_code;

  static std::string definition(bool current, int32_t code);

public:
  epsg() : m_code(-1)  {}
  explicit epsg(int32_t code);
  bool operator ==(const epsg& r) const  { return m_code == r.m_code; }
  operator projPJ() const  { return m_res? m_res->pj: 0; }
}; // epsg

inline std::string epsg::definition(bool current, int32_t code)
{
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << "+init=";
  if (current) stream << "./";
  stream << "epsg:";
  stream << code;
  return stream.str();
}

inline epsg::epsg(int32_t code)
{
  if (detail::lib::singleton().empty()) throw std::runtime_error("projection error");
  projPJ pj(0);
  if (!pj) pj = detail::lib::singleton().p_pj_init_plus( definition(true, code).c_str() );
  if (!pj) pj = detail::lib::singleton().p_pj_init_plus( definition(false, code).c_str() );
  if (!pj) throw std::runtime_error("projection error");
  m_res = std::shared_ptr<resource>(new resource(pj));
  m_code = code;
} // epsg::

} } // brig::proj

#endif // BRIG_PROJ_EPSG_HPP
