clc
clear all
close all

cal_us = 3453729;
cal_cyc = 823452;

cpumicrosoverhead = cal_us/cal_cyc;

fcpu = 80e6;

tclk = 1/fcpu;

cpusecsoh = cpumicrosoverhead * 1e-6;

#cpucycoh = round(cpusecsoh / tclk);

cpucycoh = (cal_us * (fcpu/1e6))/cal_cyc;

treportms = 1000;

cpucycoh = 338; # overhead for cycles counter and branch cpu cycles

exp_cycles = (fcpu/1000) * treportms;# / cpucycoh;

noload_cycles = 82258; # no load cpu cycles over 1000ms
med_cycles = 22428;

cycles = [noload_cycles, med_cycles];

load = 100 - 100 * cycles * cpucycoh ./ exp_cycles;

load



