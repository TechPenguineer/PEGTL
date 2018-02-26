// Copyright (c) 2018 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/taocpp/PEGTL/

#include <algorithm>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <cassert>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include <tao/pegtl.hpp>
#include <tao/pegtl/analyze.hpp>
#include <tao/pegtl/contrib/abnf.hpp>
#include <tao/pegtl/contrib/parse_tree.hpp>

namespace tao
{
   namespace TAO_PEGTL_NAMESPACE
   {
      namespace abnf
      {
         std::string to_string( const std::unique_ptr< tao::TAO_PEGTL_NAMESPACE::parse_tree::node >& n );
         std::string to_string( const std::vector< std::unique_ptr< tao::TAO_PEGTL_NAMESPACE::parse_tree::node > >& v );

         namespace grammar
         {
            // ABNF grammar according to RFC 5234, updated by RFC 7405, with
            // the following differences:
            //
            // To form a C++ identifier from a rulename, all minuses are
            // replaced with underscores.
            //
            // As C++ identifiers are case-sensitive, we remember the "correct"
            // spelling from the first occurrence of a rulename, all other
            // occurrences are automatically changed to that.
            //
            // Certain rulenames are reserved as their equivalent C++ identifier is
            // reserved as a keyword, an alternative token, by the standard or
            // for other, special reasons.
            //
            // When using numerical values (num-val, repeat), the values
            // must be in the range of the corresponsing C++ data type.
            //
            // Remember we are defining a PEG, not a CFG. Simply copying some
            // ABNF from somewhere might lead to surprising results as the
            // alternations are now sequential, using the sor<> rule.
            //
            // PEG also require two extensions: the and-predicate and the
            // not-predicate. They are expressed by '&' and '!' respectively,
            // being allowed (optionally, only one of them) before the
            // repetition. You can use braces for more complex expressions.
            //
            // Finally, instead of the pre-defined CRLF sequence, we accept
            // any type of line ending as a convencience extension:

            // clang-format off
            struct CRLF : sor< abnf::CRLF, CR, LF > {};

            // The rest is according to the RFC(s):
            struct comment_cont : until< CRLF, sor< WSP, VCHAR > > {};
            struct comment : if_must< one< ';' >, comment_cont > {};
            struct c_nl : sor< comment, CRLF > {};
            struct c_wsp : sor< WSP, seq< c_nl, WSP > > {};

            struct rulename : seq< ALPHA, star< ranges< 'a', 'z', 'A', 'Z', '0', '9', '-' > > > {};

            struct quoted_string_cont : until< DQUOTE, print > {};
            struct quoted_string : if_must< DQUOTE, quoted_string_cont > {};
            struct case_insensitive_string : seq< opt< istring< '%', 'i' > >, quoted_string > {};
            struct case_sensitive_string : seq< istring< '%', 's' >, quoted_string > {};
            struct char_val : sor< case_insensitive_string, case_sensitive_string > {};

            struct prose_val_cont : until< one< '>' >, print > {};
            struct prose_val : if_must< one< '<' >, prose_val_cont > {};

            template< char First, typename Digit >
            struct gen_val
            {
               struct value : plus< Digit > {};
               struct range : if_must< one< '-' >, value > {};
               struct next_value : must< value > {};
               struct type : seq< istring< First >, must< value >, sor< range, star< one< '.' >, next_value > > > {};
            };

            using hex_val = gen_val< 'x', HEXDIG >;
            using dec_val = gen_val< 'd', DIGIT >;
            using bin_val = gen_val< 'b', BIT >;

            struct num_val_choice : sor< bin_val::type, dec_val::type, hex_val::type > {};
            struct num_val : if_must< one< '%' >, num_val_choice > {};

            struct alternation;
            struct option_close : one< ']' > {};
            struct option : seq< one< '[' >, pad< must< alternation >, c_wsp >, must< option_close > > {};
            struct group_close : one< ')' > {};
            struct group : seq< one< '(' >, pad< must< alternation >, c_wsp >, must< group_close > > {};
            struct element : sor< rulename, group, option, char_val, num_val, prose_val > {};

            struct repeat : sor< seq< star< DIGIT >, one< '*' >, star< DIGIT > >, plus< DIGIT > > {};
            struct repetition : seq< opt< repeat >, element > {};

            struct and_predicate : if_must< one< '&' >, repetition > {};
            struct not_predicate : if_must< one< '!' >, repetition > {};
            struct predicate : sor< and_predicate, not_predicate, repetition > {};

            struct concatenation : list< predicate, plus< c_wsp > > {};
            struct alternation : list_must< concatenation, pad< one< '/' >, c_wsp > > {};

            struct defined_as_op : sor< string< '=', '/' >, one< '=' > > {};
            struct defined_as : pad< defined_as_op, c_wsp > {};
            struct rule : seq< if_must< rulename, defined_as, alternation >, star< c_wsp >, must< c_nl > > {};
            struct rulelist : until< eof, sor< seq< star< c_wsp >, c_nl >, must< rule > > > {};

            // end of grammar

            template< typename Rule >
            struct error_control : normal< Rule >
            {
               static const std::string error_message;

               template< typename Input, typename... States >
               static void raise( const Input& in, States&&... /*unused*/ )
               {
                  throw parse_error( error_message, in );
               }
            };

            template<> const std::string error_control< comment_cont >::error_message = "unterminated comment";  // NOLINT

            template<> const std::string error_control< quoted_string_cont >::error_message = "unterminated string (missing '\"')";  // NOLINT
            template<> const std::string error_control< prose_val_cont >::error_message = "unterminated prose description (missing '>')";  // NOLINT

            template<> const std::string error_control< hex_val::value >::error_message = "expected hexadecimal value";  // NOLINT
            template<> const std::string error_control< dec_val::value >::error_message = "expected decimal value";  // NOLINT
            template<> const std::string error_control< bin_val::value >::error_message = "expected binary value";  // NOLINT
            template<> const std::string error_control< num_val_choice >::error_message = "expected base specifier (one of 'bBdDxX')";  // NOLINT

            template<> const std::string error_control< option_close >::error_message = "unterminated option (missing ']')";  // NOLINT
            template<> const std::string error_control< group_close >::error_message = "unterminated group (missing ')')";  // NOLINT

            template<> const std::string error_control< repetition >::error_message = "expected element";  // NOLINT
            template<> const std::string error_control< concatenation >::error_message = "expected element";  // NOLINT
            template<> const std::string error_control< alternation >::error_message = "expected element";  // NOLINT

            template<> const std::string error_control< defined_as >::error_message = "expected '=' or '=/'";  // NOLINT
            template<> const std::string error_control< c_nl >::error_message = "unterminated rule";  // NOLINT
            template<> const std::string error_control< rule >::error_message = "expected rule";  // NOLINT
            // clang-format on

            // clang-format off
            template< typename Rule > struct selector : std::false_type {};
            template<> struct selector< rulename > : std::true_type {};
            template<> struct selector< quoted_string > : std::true_type {};
            template<> struct selector< case_sensitive_string > : std::true_type {};
            template<> struct selector< prose_val > : std::true_type {};
            template<> struct selector< hex_val::value > : std::true_type {};
            template<> struct selector< dec_val::value > : std::true_type {};
            template<> struct selector< bin_val::value > : std::true_type {};
            template<> struct selector< hex_val::range > : std::true_type {};
            template<> struct selector< dec_val::range > : std::true_type {};
            template<> struct selector< bin_val::range > : std::true_type {};
            template<> struct selector< hex_val::type > : std::true_type {};
            template<> struct selector< dec_val::type > : std::true_type {};
            template<> struct selector< bin_val::type > : std::true_type {};
            template<> struct selector< alternation > : std::true_type {};
            template<> struct selector< option > : std::true_type {};
            template<> struct selector< group > : std::true_type {};
            template<> struct selector< repeat > : std::true_type {};
            template<> struct selector< repetition > : std::true_type {};
            template<> struct selector< and_predicate > : std::true_type {};
            template<> struct selector< not_predicate > : std::true_type {};
            template<> struct selector< concatenation > : std::true_type {};
            template<> struct selector< rule > : std::true_type {};
            // clang-format on

         }  // namespace grammar

         namespace
         {
            // clang-format off
            std::set< std::string > keywords = {  // NOLINT
               "alignas", "alignof", "and", "and_eq",
               "asm", "auto", "bitand", "bitor",
               "bool", "break", "case", "catch",
               "char", "char16_t", "char32_t", "class",
               "compl", "const", "constexpr", "const_cast",
               "continue", "decltype", "default", "delete",
               "do", "double", "dynamic_cast", "else",
               "enum", "explicit", "export", "extern",
               "false", "float", "for", "friend",
               "goto", "if", "inline", "int",
               "long", "mutable", "namespace", "new",
               "noexcept", "not", "not_eq", "nullptr",
               "operator", "or", "or_eq", "private",
               "protected", "public", "register", "reinterpret_cast",
               "return", "short", "signed", "sizeof",
               "static", "static_assert", "static_cast", "struct",
               "switch", "template", "this", "thread_local",
               "throw", "true", "try", "typedef",
               "typeid", "typename", "union", "unsigned",
               "using", "virtual", "void", "volatile",
               "wchar_t", "while", "xor", "xor_eq"
            };
            // clang-format on

            std::string prefix = "tao::pegtl::";

            using rules_t = std::vector< std::string >;
            rules_t rules_defined;
            rules_t rules;

            rules_t::reverse_iterator find_rule( rules_t& r, const std::string& v, const rules_t::reverse_iterator& rbegin )
            {
               return std::find_if( rbegin, r.rend(), [&]( const rules_t::value_type& p ) { return ::strcasecmp( p.c_str(), v.c_str() ) == 0; } );
            }

            rules_t::reverse_iterator find_rule( rules_t& r, const std::string& v )
            {
               return find_rule( r, v, r.rbegin() );
            }

            std::string get_rulename( const std::unique_ptr< tao::TAO_PEGTL_NAMESPACE::parse_tree::node >& n )
            {
               assert( n->is< grammar::rulename >() );
               std::string v = n->content();
               std::replace( v.begin(), v.end(), '-', '_' );
               return v;
            }

            std::string get_rulename( const std::unique_ptr< tao::TAO_PEGTL_NAMESPACE::parse_tree::node >& n, const bool print_forward_declarations )
            {
               std::string v = get_rulename( n );
               const auto it = find_rule( rules, v );
               if( it != rules.rend() ) {
                  return *it;
               }
               if( keywords.count( v ) != 0 || v.find( "__" ) != std::string::npos ) {
                  throw std::runtime_error( to_string( n->begin() ) + ": '" + v + "' is a reserved rulename" );
               }
               if( print_forward_declarations && find_rule( rules_defined, v ) != rules_defined.rend() ) {
                  std::cout << "struct " << v << ";\n";
               }
               rules.push_back( v );
               return v;
            }

            bool append_char( std::string& s, const char c )
            {
               if( !s.empty() ) {
                  s += ", ";
               }
               s += '\'';
               if( c == '\'' || c == '\\' ) {
                  s += '\\';
               }
               s += c;
               s += '\'';
               return std::isalpha( c ) != 0;
            }

            template< typename T >
            std::string gen_val( const std::unique_ptr< tao::TAO_PEGTL_NAMESPACE::parse_tree::node >& n )
            {
               if( n->children.size() == 2 ) {
                  if( n->children.back()->is< T >() ) {
                     return prefix + "range< " + to_string( n->children.front() ) + ", " + to_string( n->children.back()->children.front() ) + " >";
                  }
               }
               if( n->children.size() == 1 ) {
                  return prefix + "one< " + to_string( n->children ) + " >";
               }
               return prefix + "string< " + to_string( n->children ) + " >";
            }

            std::string remove_leading_zeroes( const std::string& v )
            {
               const auto pos = v.find_first_not_of( '0' );
               if( pos == std::string::npos ) {
                  return "";
               }
               return v.substr( pos );
            }

         }  // namespace

         std::string to_string( const std::unique_ptr< tao::TAO_PEGTL_NAMESPACE::parse_tree::node >& n )
         {
            // rulename
            if( n->is< grammar::rulename >() ) {
               return get_rulename( n, true );
            }

            // quoted_string
            if( n->is< grammar::quoted_string >() ) {
               const std::string content = n->content();
               std::string s;
               bool alpha = false;
               for( const auto c : content.substr( 1, content.size() - 2 ) ) {
                  alpha = append_char( s, c ) || alpha;
               }
               if( alpha ) {
                  return prefix + "istring< " + s + " >";
               }
               if( content.size() > 3 ) {
                  return prefix + "string< " + s + " >";
               }
               return prefix + "one< " + s + " >";
            }

            // case_sensitive_string
            if( n->is< grammar::case_sensitive_string >() ) {
               const std::string content = n->content();
               std::string s;
               for( const auto c : content.substr( 1, content.size() - 2 ) ) {
                  append_char( s, c );
               }
               if( content.size() > 3 ) {
                  return prefix + "string< " + s + " >";
               }
               return prefix + "one< " + s + " >";
            }

            // prose_val
            if( n->is< grammar::prose_val >() ) {
               return "/* " + n->content() + " */";
            }

            // hex_val::value
            if( n->is< grammar::hex_val::value >() ) {
               return "0x" + n->content();
            }

            // hex_val::type
            if( n->is< grammar::hex_val::type >() ) {
               return gen_val< grammar::hex_val::range >( n );
            }

            // dec_val::value
            if( n->is< grammar::dec_val::value >() ) {
               return n->content();
            }

            // dec_val::type
            if( n->is< grammar::dec_val::type >() ) {
               return gen_val< grammar::dec_val::range >( n );
            }

            // bin_val::value
            if( n->is< grammar::bin_val::value >() ) {
               return std::to_string( std::strtoull( n->content().c_str(), nullptr, 2 ) );
            }

            // bin_val::type
            if( n->is< grammar::bin_val::type >() ) {
               return gen_val< grammar::bin_val::range >( n );
            }

            // alternation
            if( n->is< grammar::alternation >() ) {
               assert( !n->children.empty() );
               if( n->children.size() == 1 ) {
                  return to_string( n->children.front() );
               }
               return prefix + "sor< " + to_string( n->children ) + " >";
            }

            // option
            if( n->is< grammar::option >() ) {
               return prefix + "opt< " + to_string( n->children ) + " >";
            }

            // group
            if( n->is< grammar::group >() ) {
               assert( !n->children.empty() );
               if( n->children.size() == 1 ) {
                  return to_string( n->children.front() );
               }
               return prefix + "seq< " + to_string( n->children ) + " >";
            }

            // repetition
            if( n->is< grammar::repetition >() ) {
               assert( !n->children.empty() );
               const auto content = to_string( n->children.back() );
               if( n->children.size() == 1 ) {
                  return content;
               }
               assert( n->children.size() == 2 );
               const auto rep = n->children.front()->content();
               const auto star = rep.find( '*' );
               if( star == std::string::npos ) {
                  const auto v = remove_leading_zeroes( rep );
                  if( v.empty() ) {
                     throw std::runtime_error( to_string( n->begin() ) + ": repetition of zero not allowed" );
                  }
                  return prefix + "rep< " + v + ", " + content + " >";
               }
               else {
                  const auto min = remove_leading_zeroes( rep.substr( 0, star ) );
                  const auto max = remove_leading_zeroes( rep.substr( star + 1 ) );
                  if( ( star != rep.size() - 1 ) && max.empty() ) {
                     throw std::runtime_error( to_string( n->begin() ) + ": repetition maximum of zero not allowed" );
                  }
                  if( min.empty() && max.empty() ) {
                     return prefix + "star< " + content + " >";
                  }
                  if( !min.empty() && max.empty() ) {
                     if( min == "1" ) {
                        return prefix + "plus< " + content + " >";
                     }
                     else {
                        return prefix + "rep_min< " + min + ", " + content + " >";
                     }
                  }
                  if( min.empty() && !max.empty() ) {
                     if( max == "1" ) {
                        return prefix + "opt< " + content + " >";
                     }
                     else {
                        return prefix + "rep_max< " + max + ", " + content + " >";
                     }
                  }
                  const auto min_val = std::strtoull( min.c_str(), nullptr, 10 );
                  const auto max_val = std::strtoull( max.c_str(), nullptr, 10 );
                  if( min_val > max_val ) {
                     throw std::runtime_error( to_string( n->begin() ) + ": repetition minimum which is greater than the repetition maximum not allowed" );
                  }
                  return prefix + "rep_min_max< " + min + ", " + max + ", " + content + " >";
               }
            }

            // and_predicate
            if( n->is< grammar::and_predicate >() ) {
               assert( n->children.size() == 1 );
               return prefix + "at< " + to_string( n->children.front() ) + " >";
            }

            // not_predicate
            if( n->is< grammar::not_predicate >() ) {
               assert( n->children.size() == 1 );
               return prefix + "not_at< " + to_string( n->children.front() ) + " >";
            }

            // concatenation
            if( n->is< grammar::concatenation >() ) {
               assert( !n->children.empty() );
               if( n->children.size() == 1 ) {
                  return to_string( n->children.front() );
               }
               return prefix + "seq< " + to_string( n->children ) + " >";
            }

            // rule
            if( n->is< grammar::rule >() ) {
               return "struct " + get_rulename( n->children.front(), false ) + " : " + to_string( n->children.back() ) + " {};";
            }

            throw std::runtime_error( to_string( n->begin() ) + ": missing to_string() for " + n->name() );
         }

         std::string to_string( const std::vector< std::unique_ptr< tao::TAO_PEGTL_NAMESPACE::parse_tree::node > >& v )
         {
            std::string result;
            for( const auto& c : v ) {
               if( !result.empty() ) {
                  result += ", ";
               }
               result += to_string( c );
            }
            return result;
         }

      }  // namespace abnf

   }  // namespace TAO_PEGTL_NAMESPACE

}  // namespace tao

int main( int argc, char** argv )
{
   using namespace tao::TAO_PEGTL_NAMESPACE;  // NOLINT

   if( argc != 2 ) {
      analyze< abnf::grammar::rulelist >();
      std::cerr << "Usage: " << argv[ 0 ] << " SOURCE" << std::endl;
      return 1;
   }

   file_input<> in( argv[ 1 ] );
   const auto root = parse_tree::parse< abnf::grammar::rulelist, abnf::grammar::selector >( in );
   for( const auto& rule : root->children ) {
      assert( rule->is< abnf::grammar::rule >() );
      abnf::rules_defined.push_back( abnf::get_rulename( rule->children.front() ) );
   }
   for( const auto& rule : root->children ) {
      std::cout << abnf::to_string( rule ) << std::endl;
   }
   return 0;
}
