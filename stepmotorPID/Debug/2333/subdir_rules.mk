################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
2333/%.o: ../2333/%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Arm Compiler - building file: "$<"'
	"D:/ti/ccs2050/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c @"device.opt"  -march=thumbv6m -mcpu=cortex-m0plus -mfloat-abi=soft -mlittle-endian -mthumb -O0 -I"D:/2333" -I"D:/2333/Debug" -I"D:/ti/ccs2050/mspm0_sdk_2_10_00_04/source/third_party/CMSIS/Core/Include" -I"D:/ti/ccs2050/mspm0_sdk_2_10_00_04/source" -gdwarf-3 -Wall -MMD -MP -MF"2333/$(basename $(<F)).d_raw" -MT"$(@)"  $(GEN_OPTS__FLAG) -o"$@" "$<"
	@echo 'Finished building: "$<"'
	@echo ' '

build-1709151349: ../2333/empty.syscfg
	@echo 'SysConfig - building file: "$<"'
	"D:/ti/ccs2050/sysconfig_1.26.2/sysconfig_cli.bat" -s "D:/ti/ccs2050/mspm0_sdk_2_10_00_04/.metadata/product.json" --script "D:/2333/2333/empty.syscfg" -o "." --compiler ticlang
	@echo 'Finished building: "$<"'
	@echo ' '

device_linker.cmd: build-1709151349 ../2333/empty.syscfg
device.opt: build-1709151349
device.cmd.genlibs: build-1709151349
ti_msp_dl_config.c: build-1709151349
ti_msp_dl_config.h: build-1709151349
Event.dot: build-1709151349


