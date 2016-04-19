#!/usr/bin/env python3

import sys, itertools
import numpy as np

if len(sys.argv) not in [2,3]:
    print("usage: {} hyperfine_cutoff_in_kHz [c13_abundance]".format(sys.argv[0]))
    exit(1)

# process inputs
hyperfine_cutoff = float(sys.argv[1])*1000  # cutoff for hyperfine field strength; Hz
try:
    c13_abundance = float(sys.argv[2])/100
except:
    c13_abundance = 0.0107 # natural abundance of C-13

# flatten a list of lists
def flatten(list):
    out = []
    for el in list: out += el
    return out

# return product of elements in a list
def product(list):
    out = 1
    for el in list: out *= el
    return out

# physical constants in SI
c_SI = 299792458 # speed of light (meters/second)
hbar_SI = 6.582119514e-16 # reduced Planck constant (eV/second)
ge_SI = -1.760859708e11 # gyromagnetic ratio of NV electron (Hz/tesla)
gC13_SI = 67.28284e6 # gyromagnetic ratio of C-13 (Hz/tesla)

# physical constants in natural units (s or Hz)
alpha = 1/137.035999074 # fine structure constant
qe = np.sqrt(4*np.pi*alpha) # unit electric charge
me = 510998.928/hbar_SI # mass of electron (Hz)
ge = -qe/(2*me) * 2.0023193043617 # gyromagnetic ratio of electron
gC13 = gC13_SI * ge/ge_SI # gyromagnetic ratio of C-13

sec = 1 # one second in natural units (s)
meter = sec / c_SI # one meter in natural units (s)
nm = 1e-9*meter # one nanometer in natural units (s)

a0 = 0.35668 * nm # diamond lattice parameter (unit cell side length) at 300 K
zhat = np.array([1,1,1])/np.sqrt(3) # static magnetic field direction; NV axis

# diamond lattice vectors
ao = np.array([1,1,1])/4
a1 = np.array([0,1,1])/2
a2 = np.array([1,0,1])/2
a3 = np.array([1,1,0])/2

norm = np.linalg.norm
def hat(v): return v/norm(v)
def A(b,l,m,n):
    r = b*ao+l*a1+m*a2+n*a3
    return norm( ge*gC13/(4*np.pi*(norm(r)*a0)**3) * (zhat - 3*np.dot(hat(r),zhat)*hat(r)) )

# maximum allowable magnitude of l, m, or n
M = int((2*abs(ge*gC13) / (np.pi * a0**3 * hyperfine_cutoff))**(1/3)+1/2)

# list of all equivalence classes of integers (b,l,m,n)
equivalence_classes = []

# loop over each quarter-integer N <= 3*M
dN = 0.25
for N in np.arange(0,3*M+dN,dN):

    # sets of integers {b,l,m,n} satisfying abs(l+m+n) == N
    sum_solutions = [ (b,l,m,n)
                      for b in [0,1]
                      for l in range(-M,M+1)
                      for m in range(-M,M+1)
                      for n in range(-M,M+1)
                      if abs(3/4*b + l+m+n) == N
                      if l != 0 or m != 0 or n != 0
                      if A(b,l,m,n) > hyperfine_cutoff ]

    # if the above set is empty, skip ahead to the next value of N
    if(len(sum_solutions) == 0): continue

    # determine all equivalence classes of integers (b,l,m,n) for this value of N
    lmn_square_sums = set([ l*l+m*m+n*n-1/3*(l+m+n)**2 for (b,l,m,n) in sum_solutions ])

    for ss in lmn_square_sums:
        equivalence_class = [ (b,l,m,n) for (b,l,m,n) in sum_solutions
                              if l*l+m*m+n*n-1/3*(l+m+n)**2 == ss ]
        equivalence_classes.append(equivalence_class)

equivalence_class_sizes = [ len(equivalence_class)
                            for equivalence_class in equivalence_classes ]

# probability of having at least one larmor pair
probability = 1 - product([ (1-c13_abundance)**R * (1 + R*c13_abundance/(1-c13_abundance))
                            for R in equivalence_class_sizes ])

print(probability)