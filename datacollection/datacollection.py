#!/usr/bin/python
# -*- coding: utf-8 -*-

import subprocess

# subprocess.check_output(args, *, stdin=None, stderr=subprocess.STDOUT, shell=False, universal_newlines=False)¶
# subprocess.call(args, *, stdin=None, stdout=None, stderr=None, shell=False)
# [IFRit] 120 395 : 0x4b9cbd 0x4bd10d
# ***[IFRit] [WX] IFR ID: 360 1214 PC: 0x44c26a 0x47c236

# build
# output = subprocess.check_output("\
# 	export LD_LIBRARY_PATH=~/IFRit/Runtime;\
# 	cd ~/parsec-2.1;\
# 	bin/parsecmgmt -a uninstall -p x264 -c gcc-pthreads;\
# 	bin/parsecmgmt -a build -p x264 -c gcc-pthreads;", \
# 	stderr=subprocess.STDOUT, shell=True)

# print output
# output = subprocess.call("export LD_LIBRARY_PATH=~/IFRit/Runtime;\
# run


for app in ["blackscholes", "fluidanimate", "swaptions", "x264", "streamcluster", "bodytrack"]:
	for inputSize in ["simsmall", "simlarge"]:
		for numThreads in [4, 8]:
			for numRaces in ["allraces", "one"]:
				for concurency in ["1lock", "32lock", "512lock", "htm"]:
					time = []
					count = 0  # total races reported
					dataraces = []  # unique races
					outFile = open(app+"-mpx-"+inputSize+"-"+str(numThreads)+"-"+numRaces+"-"+concurency+".temp.txt" ,'w') 
					print "Running", app, inputSize, numThreads, numRaces, concurency
					print "bin/parsecmgmt -a run -p "+app+" -c gcc-pthreads -n "+str(numThreads)+" -i "+inputSize, "\n\n"

					for x in xrange(0, 5):

						output = subprocess.check_output("export LD_LIBRARY_PATH=~/IFRit/Runtime;\
							cd ~/parsec-2.1;\
							cp -f /root/IFRit/Runtime/libIFR_Runtime_"+numRaces+".so."+concurency+  " /root/IFRit/Runtime/libIFR_Runtime.so;  \
							bin/parsecmgmt -a run -p "+app+" -c gcc-pthreads -n "+str(numThreads)+" -i "+inputSize+";\
							exit 0;", 
							stderr=subprocess.STDOUT, shell=True)
						# print "cp -f /root/IFRit/Runtime/libIFR_Runtime_"+numRaces+".so."+concurency+  "/root/IFRit/Runtime/libIFR_Runtime.so; "
						# print output
						# exit(0)
						splitoutput = output.split("\n")
						for x in splitoutput:
							if "real" in x:
								time.append(x)
							if "IFRit" in x and ":" in x:
								IFR1 = 0
								PC1 = ""
								IFR2 = 0
								PC2 = ""
								count = count + 1

								for y in x.split():
									if y.isdigit():
										if IFR1 == 0:
											IFR1 = int(y)
											PC1 = x.split()[-2]
										else:
											IFR2 = int(y)
											PC2 = x.split()[-1]
											break

								pair = ""
								if IFR1 < IFR2:
									pair = str(IFR1) + "\t" + str(IFR2)	 + "\t\t" + PC1 + "\t" + PC2
								else:
									pair = str(IFR2) + "\t" + str(IFR1)	 + "\t\t" + PC2 + "\t" + PC1		

								if pair not in dataraces:
									dataraces.append(pair)

					for x in time:
						outFile.write(x+"\n")
					outFile.write("=================================\n")
					outFile.write("num races = " + str(count) + "\n")
					outFile.write("num unique races = " + str(len(dataraces)) + "\n")
					outFile.write("=================================\n")
					for x in dataraces:
						outFile.write(x + "\n")
					outFile.close()




for app in ["blackscholes", "fluidanimate", "swaptions", "x264", "streamcluster", "bodytrack"]:
	for inputSize in ["simsmall", "simlarge"]:
		for numThreads in [4, 8]:
			for concurency in ["1lock", "32lock", "512lock"]:
				time = []
				count = 0  # total races reported
				dataraces = []  # unique races
				outFile = open(app+"-hashtable-"+inputSize+"-"+str(numThreads)+"-"+concurency+".temp.txt" ,'w') 
				print "Running", app, inputSize, numThreads, "hashtable", concurency, "\n\n"

				for x in xrange(0, 5):
					output = subprocess.check_output("export LD_LIBRARY_PATH=~/IFRit/Runtime;\
						cd ~/parsec-2.1;\
						cp -f /root/IFRit/Runtime/libIFR_Runtime_orig.so."+concurency+  " /root/IFRit/Runtime/libIFR_Runtime.so;  \
						bin/parsecmgmt -a run -p "+app+" -c gcc-pthreads -n "+str(numThreads)+" -i "+inputSize+";\
						exit 0;", 
						stderr=subprocess.STDOUT, shell=True)

					splitoutput = output.split("\n")
					for x in splitoutput:
						if "real" in x:
							time.append(x)
						if "IFRit" in x and ":" in x:
							IFR1 = 0
							PC1 = ""
							IFR2 = 0
							PC2 = ""
							count = count + 1

							for y in x.split():
								if y.isdigit():
									if IFR1 == 0:
										IFR1 = int(y)
										PC1 = x.split()[-2]
									else:
										IFR2 = int(y)
										PC2 = x.split()[-1]
										break

							pair = ""
							if IFR1 < IFR2:
								pair = str(IFR1) + "\t" + str(IFR2)	 + "\t\t" + PC1 + "\t" + PC2
							else:
								pair = str(IFR2) + "\t" + str(IFR1)	 + "\t\t" + PC2 + "\t" + PC1		

							if pair not in dataraces:
								dataraces.append(pair)

				for x in time:
					outFile.write(x+"\n")
				outFile.write("=================================\n")
				outFile.write("num races = " + str(count) + "\n")
				outFile.write("num unique races = " + str(len(dataraces)) + "\n")
				outFile.write("=================================\n")
				for x in dataraces:
					outFile.write(x + "\n")
				outFile.close()




