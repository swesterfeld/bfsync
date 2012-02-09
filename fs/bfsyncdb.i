/* bfsyncdb.i */
%module bfsyncdb
%{
#include "bfsyncdb.hh"
%}

%include "std_string.i"
%include "std_vector.i"
%include "stdint.i"

// Instantiate templates used
namespace std {
  %template(StringVector) vector<string>;
  %template(LinkVector) vector<Link>;
  %template(INodeVector) vector<INode>;
  %template(UIntVector) vector<unsigned int>;
}

%include "bfsyncdb.hh"
