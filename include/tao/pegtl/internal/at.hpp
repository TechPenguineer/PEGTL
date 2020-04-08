// Copyright (c) 2014-2020 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/taocpp/PEGTL/

#ifndef TAO_PEGTL_INTERNAL_AT_HPP
#define TAO_PEGTL_INTERNAL_AT_HPP

#include "../config.hpp"

#include "seq.hpp"
#include "skip_control.hpp"
#include "success.hpp"

#include "../apply_mode.hpp"
#include "../rewind_mode.hpp"
#include "../rule_list.hpp"

namespace TAO_PEGTL_NAMESPACE::internal
{
   template< typename... Rules >
   struct at
      : at< seq< Rules... > >
   {};

   template<>
   struct at<>
      : success
   {};

   template< typename Rule >
   struct at< Rule >
   {
      using rule_t = at;
      using subs_t = rule_list< Rule >;

      template< apply_mode,
                rewind_mode,
                template< typename... >
                class Action,
                template< typename... >
                class Control,
                typename Input,
                typename... States >
      [[nodiscard]] static bool match( Input& in, States&&... st )
      {
         const auto m = in.template mark< rewind_mode::required >();
         return Control< Rule >::template match< apply_mode::nothing, rewind_mode::active, Action, Control >( in, st... );
      }
   };

   template< typename... Rules >
   inline constexpr bool skip_control< at< Rules... > > = true;

}  // namespace TAO_PEGTL_NAMESPACE::internal

#endif
