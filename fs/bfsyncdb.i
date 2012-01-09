/* bfsyncdb.i */
%module bfsyncdb
%{
#include "bfsyncdb.hh"
%}

%include "std_string.i"
%include "std_vector.i"

// Instantiate templates used
namespace std {
   %template(LinkVector) vector<Link>;
   %template(INodeVector) vector<INode>;
}

%include "bfsyncdb.hh"
