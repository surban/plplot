## Copyright (C) 1998-2002 Joao Cardoso.
## 
## This program is free software; you can redistribute it and/or modify it
## under the terms of the GNU General Public License as published by the
## Free Software Foundation; either version 2 of the License, or (at your
## option) any later version.
## 
## This program is distributed in the hope that it will be useful, but
## WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
## General Public License for more details.
##
## This file is part of plplot_octave.

## plclg() or plclearplot (): clear the plot window

function plclearplot

  global __pl

  if (!exist("__pl") || !struct_contains (__pl,"inited"))
    plplot_stub;
  endif

  plclear;
  plflush;

endfunction                
