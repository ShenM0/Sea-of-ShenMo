################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Arm Compiler - building file: "$<"'
	"D:/ti/ccs2050/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c @"device.opt"  -march=thumbv6m -mcpu=cortex-m0plus -mfloat-abi=soft -mlittle-endian -mthumb -O0 -I"C:/Users/ky118/Desktop/233/2333/2333" -I"C:/Users/ky118/Desktop/233/2333/2333/Debug" -I"D:/ti/ccs2050/mspm0_sdk_2_10_00_04/source/third_party/CMSIS/Core/Include" -I"D:/ti/ccs2050/mspm0_sdk_2_10_00_04/source" -gdwarf-3 -Wall -MMD -MP -MF"$(basename $(<F)).d_raw" -MT"$(@)"  $(GEN_OPTS__FLAG) -o"$@" "$<"
	@echo 'Finished building: "$<"'
	@echo ' '

build-396677138: ../empty.syscfg
	@echo 'SysConfig - building file: "$<"'
	"D:/ti/ccs2050/sysconfig_1.26.2/sysconfig_cli.bat" -s "D:/ti/ccs2050/mspm0_sdk_2_10_00_04/.metadata/product.json" --script "C:/Users/ky118/Desktop/233/2333/2333/empty.syscfg" -o "." --compiler ticlang
	@echo 'Finished building: "$<"'
	@echo ' '

device_linker.cmd: build-396677138 ../empty.syscfg
device.opt: build-396677138
device.cmd.genlibs: build-396677138
ti_msp_dl_config.c: build-396677138
ti_msp_dl_config.h: build-396677138
Event.dot: build-396677138

%.o: ./%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Arm Compiler - building file: "$<"'
	"D:/ti/ccs2050/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c @"device.opt"  -march=thumbv6m -mcpu=cortex-m0plus -mfloat-abi=soft -mlittle-endian -mthumb -O0 -I"C:/Users/ky118/Desktop/233/2333/2333" -I"C:/Users/ky118/Desktop/233/2333/2333/Debug" -I"D:/ti/ccs2050/mspm0_sdk_2_10_00_04/source/third_party/CMSIS/Core/Include" -I"D:/ti/ccs2050/mspm0_sdk_2_10_00_04/source" -gdwarf-3 -Wall -MMD -MP -MF"$(basename $(<F)).d_raw" -MT"$(@)"  $(GEN_OPTS__FLAG) -o"$@" "$<"
	@echo 'Finished building: "$<"'
	@echo ' '

startup_mspm0g350x_ticlang.o: D:/ti/ccs2050/mspm0_sdk_2_10_00_04/source/ti/devices/msp/m0p/startup_system_files/ticlang/startup_mspm0g350x_ticlang.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Arm Compiler - building file: "$<"'
	"D:/ti/ccs2050/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c @"device.opt"  -march=thumbv6m -mcpu=cortex-m0plus -mfloat-abi=soft -mlittle-endian -mthumb -O0 -I"C:/Users/ky118/Desktop/233/2333/2333" -I"C:/Users/ky118/Desktop/233/2333/2333/Debug" -I"D:/ti/ccs2050/mspm0_sdk_2_10_00_04/source/third_party/CMSIS/Core/Include" -I"D:/ti/ccs2050/mspm0_sdk_2_10_00_04/source" -gdwarf-3 -Wall -MMD -MP -MF"$(basename $(<F)).d_raw" -MT"$(@)"  $(GEN_OPTS__FLAG) -o"$@" "$<"
	@echo 'Finished building: "$<"'
	@echo ' '


