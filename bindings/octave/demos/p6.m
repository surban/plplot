## Copyright (C) 1998, 1999, 2000 Joao Cardoso.
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

function p6

  [x y z] = rosenbrock; z = log(z);

  t = automatic_replot;
  as = autostyle;

  autostyle "off";
  automatic_replot = 0;
  
  title("Contour example");
  contour(x,y,z)

  automatic_replot = t;
  autostyle(as);

endfunction
