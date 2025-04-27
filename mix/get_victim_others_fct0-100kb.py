 # -*- coding: UTF-8 -*- 
import numpy as np
import pandas as pd
import sys
import os
import math

pth = sys.argv[1]
out_pth = sys.argv[2]
pattern = sys.argv[3] # such as th2-poisson
calcu_line = sys.argv[4] # if fct = 10

file_list = os.listdir(pth)
out_file = open(out_pth+"/get_victime_others_fct-0-100kb.data",'w')
out_file2 = open(out_pth+"/get_victime_others_slowdown-0-100kb.data",'w')

for ff in file_list:
    if pattern in ff:
        print(ff)
        fct_pd = pd.read_csv(pth+'/'+ff, header=None, sep='\t')
        
        
        fct_pd = fct_pd[[0,1,2,9,11]] #srcid, dstid, size, fct, slowdown
        fct_pd.columns=['srcid','dstid','size','fct','slowdown']
        print(fct_pd.info()) 
        # print(fct_pd)

    #-----------------get data：incast victim others     
        pd_dst1_15 = fct_pd.loc[((fct_pd['dstid']>=1)&(fct_pd['dstid']<=15)) | ((fct_pd['srcid']>=0)&(fct_pd['srcid']<= 15))]
        pd_dst16_159 = fct_pd.loc[((fct_pd['dstid']>=16)&(fct_pd['dstid']<=159)) & ((fct_pd['srcid']>=16)&(fct_pd['srcid']<=159))]

        
        pd_dst1_15 = pd_dst1_15[pd_dst1_15['size'] <= 1e5]
        pd_dst16_159 = pd_dst16_159[pd_dst16_159['size'] <= 1e5]
        
    #-----------------get fct：incast victim others
        fct_pd_dst1_15 = pd_dst1_15.sort_values(by='fct',ascending=True)
        fct_pd_dst16_159 = pd_dst16_159.sort_values(by='fct',ascending=True)

        fct_pd_dst1_15 = fct_pd_dst1_15.reset_index(drop=True)
        fct_pd_dst16_159 = fct_pd_dst16_159.reset_index(drop=True)

        print(fct_pd_dst1_15)
        print(fct_pd_dst16_159)

        fct_dst1_15 = fct_pd_dst1_15['fct']
        fct_dst16_159 = fct_pd_dst16_159['fct']

        num_dst1_15 = len(fct_dst1_15)
        num_dst16_159 = len(fct_dst16_159)
        print(num_dst1_15)
        print(num_dst16_159)

        if num_dst16_159==0:
            continue
        elif num_dst1_15==0:
            continue
        
        out_file.write(ff+'==================================\n')
        out_file.write("poisson victime avg 25th 50th 90th 95th 99th fct:\n")
        out_file.write(str(sum(fct_dst1_15)*1.0/num_dst1_15)+'\n')
        out_file.write(str(fct_dst1_15[int(round(0.25*num_dst1_15))-1])+'\n')
        out_file.write(str(fct_dst1_15[int(round(0.50*num_dst1_15))-1])+'\n')
        out_file.write(str(fct_dst1_15[int(round(0.90*num_dst1_15))-1])+'\n')
        out_file.write(str(fct_dst1_15[int(round(0.95*num_dst1_15))-1])+'\n')
        out_file.write(str(fct_dst1_15[int(round(0.99*num_dst1_15))-1])+'\n')

        out_file.write("poisson others avg 25th 50th 90th 95th 99th fct:\n")
        out_file.write(str(sum(fct_dst16_159)*1.0/num_dst16_159)+'\n')
        out_file.write(str(fct_dst16_159[int(round(0.25*num_dst16_159))-1])+'\n')
        out_file.write(str(fct_dst16_159[int(round(0.50*num_dst16_159))-1])+'\n')
        out_file.write(str(fct_dst16_159[int(round(0.90*num_dst16_159))-1])+'\n')
        out_file.write(str(fct_dst16_159[int(round(0.95*num_dst16_159))-1])+'\n')
        out_file.write(str(fct_dst16_159[int(round(0.99*num_dst16_159))-1])+'\n')

    #-----------------get slowdown：incast victim others
        slowdown_pd_dst1_15 = pd_dst1_15.sort_values(by='slowdown',ascending=True)
        slowdown_pd_dst16_159 = pd_dst16_159.sort_values(by='slowdown',ascending=True)

        slowdown_pd_dst1_15 = slowdown_pd_dst1_15.reset_index(drop=True)
        slowdown_pd_dst16_159 = slowdown_pd_dst16_159.reset_index(drop=True)

        print(slowdown_pd_dst1_15)
        print(slowdown_pd_dst16_159)

        slowdown_dst1_15 = slowdown_pd_dst1_15['slowdown']
        slowdown_dst16_159 = slowdown_pd_dst16_159['slowdown']

        num_dst1_15 = len(slowdown_dst1_15)
        num_dst16_159 = len(slowdown_dst16_159)
        print(num_dst1_15)
        print(num_dst16_159)

        if num_dst16_159==0:
            continue
        elif num_dst1_15==0:
            continue
        
        out_file2.write(ff+'==================================\n')
        out_file2.write("poisson victime avg 25th 50th 90th 95th 99th slowdown:\n")
        out_file2.write(str(sum(slowdown_dst1_15)*1.0/num_dst1_15)+'\n')
        out_file2.write(str(slowdown_dst1_15[int(round(0.25*num_dst1_15))-1])+'\n')
        out_file2.write(str(slowdown_dst1_15[int(round(0.50*num_dst1_15))-1])+'\n')
        out_file2.write(str(slowdown_dst1_15[int(round(0.90*num_dst1_15))-1])+'\n')
        out_file2.write(str(slowdown_dst1_15[int(round(0.95*num_dst1_15))-1])+'\n')
        out_file2.write(str(slowdown_dst1_15[int(round(0.99*num_dst1_15))-1])+'\n')

        out_file2.write("poisson others avg 25th 50th 90th 95th 99th slowdown:\n")
        out_file2.write(str(sum(slowdown_dst16_159)*1.0/num_dst16_159)+'\n')
        out_file2.write(str(slowdown_dst16_159[int(round(0.25*num_dst16_159))-1])+'\n')
        out_file2.write(str(slowdown_dst16_159[int(round(0.50*num_dst16_159))-1])+'\n')
        out_file2.write(str(slowdown_dst16_159[int(round(0.90*num_dst16_159))-1])+'\n')
        out_file2.write(str(slowdown_dst16_159[int(round(0.95*num_dst16_159))-1])+'\n')
        out_file2.write(str(slowdown_dst16_159[int(round(0.99*num_dst16_159))-1])+'\n')

out_file.close()
out_file2.close()