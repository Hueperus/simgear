// Conversion functions to convert C++ types to Nasal types
//
// Copyright (C) 2012  Thomas Geymayer <tomgey@gmail.com>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Library General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA

#ifndef SG_TO_NASAL_HXX_
#define SG_TO_NASAL_HXX_

#include <simgear/nasal/nasal.h>

#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_arithmetic.hpp>

#include <string>
#include <vector>

namespace nasal
{
  class Hash;

  /**
   * Convert std::string to Nasal string
   */
  naRef to_nasal(naContext c, const std::string& str);

  /**
   * Convert function pointer to Nasal function
   */
  naRef to_nasal(naContext c, naCFunction func);

  /**
   * Convert a nasal::Hash to a Nasal hash
   */
  naRef to_nasal(naContext c, const Hash& hash);

  /**
   * Simple pass-through of naRef types to allow generic usage of to_nasal
   */
  naRef to_nasal(naContext c, naRef ref);

  /**
   * Convert a numeric type to Nasal number
   */
  template<class T>
  typename boost::enable_if< boost::is_arithmetic<T>, naRef >::type
  to_nasal(naContext c, T num)
  {
    return naNum(num);
  }

  /**
   * Convert std::vector to Nasal vector
   */
  template<class T>
  naRef to_nasal(naContext c, const std::vector<T>& vec)
  {
    naRef ret = naNewVector(c);
    naVec_setsize(c, ret, vec.size());
    for(size_t i = 0; i < vec.size(); ++i)
      naVec_set(ret, i, to_nasal(c, vec[i]));
    return ret;
  }

} // namespace nasal

#endif /* SG_TO_NASAL_HXX_ */