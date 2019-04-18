#!/bin/bash


# first make sure we are running from the base of the kernel directory
if [ ! -e MAINTAINERS  ] ; then
  echo "ERROR: You must run this script from the base of the linux-4.19 directory"
  echo "press enter to continue"
  read dummy
  exit
fi

#defaults
boardname=toaster
companyname=mycompany
devicetype=RZ_A1H

extal=13.33MHz
extalspeed=13330000
p1clockspeed=66666666
hasusbxtal=no

hasrtcxtal=no

memory="Internal RAM only"
memoryaddr=20000000
memorysize=00A00000
memorysizename=10Mbyte

haslcd=no

scif=2

hassdhi=no

hasmmc=no
haseth=no

usb0=no
usb1=no

while [ "1" == "1" ]
do
	if [ "$devicetype" != "RZ_A2M" ] ; then
		DTS_FILENAME=r7s72100-${boardname} # RZ/A1
	else
		DTS_FILENAME=r7s9210-${boardname}  # RZ/A2
	fi

	echo 'whiptail --title "Add new BSP Board"  --noitem --menu "Make changes the items below as needed, then select Create BSP.\nYou may use ESC+ESC to cancel." 0 0 0 \' > /tmp/whipcmd.txt
	echo '"         Board Name: $boardname" "" \' >> /tmp/whipcmd.txt
	echo '"       Company Name: $companyname" "" \' >> /tmp/whipcmd.txt
	echo '"        Device Type: $devicetype" "" \' >> /tmp/whipcmd.txt
	echo '"              EXTAL: $extal" "" \' >> /tmp/whipcmd.txt
      if [ "$devicetype" != "RZ_A2M" ] ; then
	echo '"     48MHz USB XTAL: $hasusbxtal" "" \' >> /tmp/whipcmd.txt
      fi
	echo '"     32kHz RTC XTAL: $hasrtcxtal" "" \' >> /tmp/whipcmd.txt
	echo '"     Serial Console: SCIF${scif}" "" \' >> /tmp/whipcmd.txt
	echo '"      System Memory: $memory" "" \' >> /tmp/whipcmd.txt
	echo '"        Memory Size: $memorysizename" "" \' >> /tmp/whipcmd.txt
	echo '"            USB ch0: $usb0" "" \' >> /tmp/whipcmd.txt
	echo '"            USB ch1: $usb1" "" \' >> /tmp/whipcmd.txt
	echo '"         eMMC Flash: $hasmmc" "" \' >> /tmp/whipcmd.txt
	echo '"     SD Card (SDIO): $hassdhi" "" \' >> /tmp/whipcmd.txt
	echo '"                LCD: $haslcd" "" \' >> /tmp/whipcmd.txt
	echo '"           Ethernet: $haseth" "" \' >> /tmp/whipcmd.txt
	echo '"       [Create BSP]" "" \' >> /tmp/whipcmd.txt
	echo '2> /tmp/answer.txt' >> /tmp/whipcmd.txt

  source /tmp/whipcmd.txt

  #ans=$(head -c 3 /tmp/answer.txt)
  ans=$(cat /tmp/answer.txt)

  # Cancel
  if [ "$ans" == "" ] ; then
    exit
    break;
  fi

  ####################################
  # boardname
  ####################################
  echo "$ans" | grep -q "Board Name:"
  if [ "$?" == "0" ] ; then
    whiptail --title "Board Name" --inputbox \
"Enter a board name.\n"\
"Please use all lower case, no spaces.\n"\
"Numbers are OK as long as the first character\n"\
"is not a number.\n"\
"This string will be used for file names, directory names and in the source code.\n"\
"This name has to be unique.\n"\
"Example: rskrza1" 0 20 \
      2> /tmp/answer.txt

    boardname=$(cat /tmp/answer.txt)

    CHECK=`grep board-${boardname} arch/arm/mach-shmobile/Makefile`
    if [ "$CHECK" != "" ] ; then
      whiptail --title "Name Conflict" --yesno "ERROR:\n      Board Name=$boardname\n    Company Name=$companyname\n\n This combination has already been used. Do you want to overwrite those files?" 0 0 2> /tmp/answer.txt

      yesno=$?
      if [ "$yesno" == "1" ] ; then
        # no
        boardname=toaster
      fi
    fi


    #check for spaces
    echo "$boardname" | grep -q " "
    if [ "$?" == "0" ] ; then
      whiptail --msgbox "ERROR:\nBoard Name contains spaces ($boardname).\nPlesae try again." 0 0
      boardname=toaster
    fi

    #check for any character other than lowercase letters or numbers
    CHECK=`echo "$boardname" | grep "^[a-z0-9]\+$"`
    if [ "$?" != "0" ] ; then
      whiptail --msgbox "ERROR:\nBoard Name contains characters other than lowercase letters and numbers $CHECK ($boardname).\nPlesae try again." 0 0
      boardname=toaster
    fi

    continue
  fi

  ####################################
  # companyname
  ####################################
  echo "$ans" | grep -q "Company Name:"
  if [ "$?" == "0" ] ; then
    whiptail --title "Board Name" --inputbox \
"Enter a company name.\n"\
"Please use all lower case, no spaces.\n"\
"Numbers are OK as long as the first character\n"\
"is not a number.\n"\
"This string will be used as the directory name for the custom boards files.\n"\
"You can use the same company name for multiple boards.\n"\
"\n"\
"Example: acme (will create directory u-boot-2017.05/board/acme/ " 0 20 \
      2> /tmp/answer.txt

    companyname=$(cat /tmp/answer.txt)

    CHECK=`grep board-${boardname} arch/arm/mach-shmobile/Makefile`
    if [ "$CHECK" != "" ] ; then
      whiptail --title "Name Conflict" --yesno "ERROR:\n      Board Name=$boardname\n    Company Name=$companyname\n\n This combination has already been used. Do you want to overwrite those files?" 0 0 2> /tmp/answer.txt

      yesno=$?
      if [ "$yesno" == "1" ] ; then
        # no
      companyname=mycompany
      fi
    fi

    #check for spaces
    echo "$companyname" | grep -q " "
    if [ "$?" == "0" ] ; then
      whiptail --msgbox "ERROR:\nCompany Name contains spaces ($companyname).\nPlesae try again." 0 0
      companyname=mycompany
    fi

    #check for any character other than lowercase letters or numbers
    CHECK=`echo "$companyname" | grep "^[a-z0-9]\+$"`
    if [ "$?" != "0" ] ; then
      whiptail --msgbox "ERROR:\nCompany Name contains characters other than lowercase letters and numbers $CHECK ($companyname).\nPlesae try again." 0 0
      companyname=mycompany
    fi

    continue
  fi

  ####################################
  # devicetype
  ####################################
  echo "$ans" | grep -q "Device Type:"
  if [ "$?" == "0" ] ; then
    whiptail --title "Device Type" --nocancel --menu "What RZ/A device are you using?" 0 0 0 \
	"RZ_A1H" "(10MB intneral RAM)" \
	"RZ_A1M" "(5MB intneral RAM)" \
	"RZ_A1L" "(3MB intneral RAM)" \
	"RZ_A2M" "(4MB intneral RAM)" \
	2> /tmp/answer.txt

    # ESC was pressed?
    if [ "$?" != "0" ] ; then continue ;  fi

    devicetype=$(cat /tmp/answer.txt)

    if [ "$memory" == "Internal RAM only" ] ; then
      memoryaddr=20000000
      if [ "$devicetype" == "RZ_A1H" ] ; then
        memorysize=00A00000
        memorysizename=10Mbyte
      elif [ "$devicetype" == "RZ_A1M" ] ; then
        memorysize=00500000
        memorysizename=5Mbyte
      elif [ "$devicetype" == "RZ_A1L" ] ; then
        memorysize=00300000
        memorysizename=3Mbyte
      elif [ "$devicetype" == "RZ_A2M" ] ; then
        memoryaddr=80000000
        memorysize=00400000
        memorysizename=4Mbyte
      fi
    fi
    continue
  fi

  ####################################
  # scif
  ####################################
  echo "$ans" | grep -q "Serial Console:"
  if [ "$?" == "0" ] ; then
    whiptail --title "Serial Console" --nocancel --menu "What SCIF channel is your serial console on?" 0 0 0 \
	"SCIF0" "TxD0/RxD0" \
	"SCIF1" "TxD1/RxD1" \
	"SCIF2" "TxD2/RxD2" \
	"SCIF3" "TxD3/RxD3" \
	"SCIF4" "TxD4/RxD4" \
	"SCIF5" "TxD5/RxD5" \
	"SCIF6" "TxD6/RxD6" \
	"SCIF7" "TxD7/RxD7" \
	2> /tmp/answer.txt

    # ESC was pressed?
    if [ "$?" != "0" ] ; then continue ;  fi

    ans=$(cat /tmp/answer.txt)
    scif=${ans:4:1}	# just save the number
    continue
  fi

  ####################################
  # memory
  ####################################
  echo "$ans" | grep -q " System Memory:"
  if [ "$?" == "0" ] ; then
    if [ "$devicetype" != "RZ_A2M" ] ; then
      whiptail --title " System Memory" --nocancel --menu "What System Memory will Linux use?" 0 0 0 \
	"Internal RAM only" "(address 0x2000000)" \
	"External SDRAM on CS2" "(address 0x0800000)" \
	"External SDRAM on CS3" "(address 0x0C00000)" \
	"External SDRAM on CS2+CS3" "(address 0x0800000) " \
	2> /tmp/answer.txt
    else
      whiptail --title " System Memory" --nocancel --menu "What System Memory will Linux use?" 0 0 0 \
	"Internal RAM only" "(address 0x80000000)" \
	"External SDRAM on CS2" "(address 0x0800000)" \
	"External SDRAM on CS3" "(address 0x0C00000)" \
	"External SDRAM on CS2+CS3" "(address 0x0800000) " \
	"External HyperRAM" "(address 0x40000000)" \
	"External OctaRAM" "(address 0x6000000)" \
	2> /tmp/answer.txt
   fi

    # ESC was pressed?
    if [ "$?" != "0" ] ; then continue ;  fi

    memory=$(cat /tmp/answer.txt)

    if [ "$memory" == "Internal RAM only" ] ; then
      memoryaddr=20000000
      if [ "$devicetype" == "RZ_A1H" ] ; then
        memorysize=00A00000
        memorysizename=10Mbyte
      elif [ "$devicetype" == "RZ_A1M" ] ; then
        memorysize=00500000
        memorysizename=5Mbyte
      elif [ "$devicetype" == "RZ_A1L" ] ; then
        memorysize=00300000
        memorysizename=3Mbyte
      elif [ "$devicetype" == "RZ_A2M" ] ; then
        memorysize=00400000
        memorysizename=4Mbyte
      fi
    fi
    if [ "$memory" == "External SDRAM on CS2" ] ; then
      memoryaddr=08000000
      memorysize=00800000
      memorysizename=8Mbyte
    fi
    if [ "$memory" == "External SDRAM on CS3" ] ; then
      memoryaddr=0C000000
      memorysize=00800000
      memorysizename=8Mbyte
    fi
    if [ "$memory" == "External SDRAM on CS2+CS3" ] ; then
      memoryaddr=08000000
      memorysize=8000000
      memorysizename=128Mbyte
    fi
    if [ "$memory" == "External HyperRAM" ] ; then
      memoryaddr=40000000
      memorysize=00800000
      memorysizename=8Mbyte
    fi
    if [ "$memory" == "External OctaRAM" ] ; then
      memoryaddr=60000000
      memorysize=00800000
      memorysizename=8Mbyte
    fi

    continue
  fi

  ####################################
  # memorysize
  ####################################
  echo "$ans" | grep -q " Memory Size:"
  if [ "$?" == "0" ] ; then

    if [ "$memory" == "Internal RAM only" ] ; then

      whiptail --msgbox "A $devicetype has $memorysizename of internal RAM.\n" 0 0
      continue
    fi

    whiptail --title " Memory Size" --nocancel --menu "What is the size your memory ($memory) ?" 0 0 0 \
	"8Mbyte" "" \
	"16Mbyte" "" \
	"32Mbyte" "" \
	"64Mbyte" "" \
	"128Mbyte" "" \
	2> /tmp/answer.txt

    # ESC was pressed?
    if [ "$?" != "0" ] ; then continue ;  fi

    memorysizename=$(cat /tmp/answer.txt)

    if [ "$memorysizename" == "8Mbyte" ] ; then
      memorysize=00800000
    elif [ "$memorysizename" == "16Mbyte" ] ; then
      memorysize=01000000
    elif [ "$memorysizename" == "32Mbyte" ] ; then
      memorysize=02000000
    elif [ "$memorysizename" == "64Mbyte" ] ; then
      memorysize=04000000
    elif [ "$memorysizename" == "128Mbyte" ] ; then
      memorysize=08000000
    fi
    continue
  fi


  ####################################
  # extal
  ####################################
  echo "$ans" | grep -q "EXTAL:"
  if [ "$?" == "0" ] ; then
    if [ "$devicetype" != "RZ_A2M" ] ; then
      whiptail --title "EXTAL"  --nocancel --menu "What speed is the EXTAL clock?" 0 0 0 \
	"10MHz" "" \
	"12MHz" "" \
	"13.33MHz" "" \
	"none" "(assumes you only have a 48 MHz USB clock)" \
	2> /tmp/answer.txt
    else
      whiptail --title "EXTAL"  --nocancel --menu "What speed is the EXTAL clock?" 0 0 0 \
	"10MHz" "" \
	"12MHz" "" \
	"20MHz" "" \
	"24MHz" "" \
	2> /tmp/answer.txt
    fi

    # ESC was pressed?
    if [ "$?" != "0" ] ; then continue ;  fi

    extal=$(cat /tmp/answer.txt)

    if [ "$devicetype" != "RZ_A2M" ] ; then
      # RZ/A1
      if [ "$extal" == "10MHz" ] ; then
        p1clockspeed=50000000
        extalspeed=10000000
      fi
      if [ "$extal" == "12MHz" ] ; then
        p1clockspeed=64000000
        extalspeed=12000000
      fi
      if [ "$extal" == "none" ] ; then
        p1clockspeed=64000000
        extalspeed=00000000
      fi
      if [ "$extal" == "13.33MHz" ] ; then
        p1clockspeed=66666666
        extalspeed=13330000
      fi
    else
      # RZ/A2
      if [ "$extal" == "10MHz" ] ; then
        p1clockspeed=55000000
        extalspeed=10000000
      fi
      if [ "$extal" == "20MHz" ] ; then
        p1clockspeed=55000000
        extalspeed=20000000
      fi
      if [ "$extal" == "12MHz" ] ; then
        p1clockspeed=66000000
        extalspeed=12000000
      fi
      if [ "$extal" == "24MHz" ] ; then
        p1clockspeed=66000000
        extalspeed=24000000
      fi
    fi

    continue
  fi


  ####################################
  # hasusbxtal
  ####################################
  echo "$ans" | grep -q "48MHz USB XTAL:"
  if [ "$?" == "0" ] ; then
    whiptail --title "48MHz USB XTAL" --nocancel --menu "Does this board have a 48MHz USB XTAL?" 0 0 0 \
	"yes" "" \
	"no" "" \
	2> /tmp/answer.txt

    # ESC was pressed?
    if [ "$?" != "0" ] ; then continue ;  fi

    hasusbxtal=$(cat /tmp/answer.txt)
    continue
  fi

  ####################################
  # hasrtcxtal
  ####################################
  echo "$ans" | grep -q "32kHz RTC XTAL:"
  if [ "$?" == "0" ] ; then
    whiptail --title "32kHz RTC XTAL" --nocancel --menu "Does this board have a 32kHz RTC XTAL?" 0 0 0 \
	"yes" "" \
	"no" "" \
	2> /tmp/answer.txt

    # ESC was pressed?
    if [ "$?" != "0" ] ; then continue ;  fi

    hasrtcxtal=$(cat /tmp/answer.txt)
    continue
  fi

  ####################################
  # hasmmc
  ####################################
  echo "$ans" | grep -q "eMMC Flash:"
  if [ "$?" == "0" ] ; then
    if [ "$devicetype" != "RZ_A2M" ] ; then
      # RZ/A1
      whiptail --title "eMMC Flash" --nocancel --menu "Does this board have eMMC Flash?" 0 0 0 \
	"yes" "" \
	"no" "" \
	2> /tmp/answer.txt
    else
      # RZ/A2
      whiptail --title "eMMC Flash" --nocancel --menu "Does this board have eMMC Flash?" 0 0 0 \
	"ch0" "SD/MMC-0" \
	"ch1" "SD/MMC-1" \
	"no" "" \
	2> /tmp/answer.txt
    fi

    # ESC was pressed?
    if [ "$?" != "0" ] ; then continue ;  fi

    hasmmc=$(cat /tmp/answer.txt)

    continue
  fi

  ####################################
  # hassdhi
  ####################################
  echo "$ans" | grep -q "SD Card (SDIO):"
  if [ "$?" == "0" ] ; then

    whiptail --title "SD Card (SDIO)" --nocancel --menu "Does this board have SD Card (SDIO)?" 0 0 0 \
	"ch0" "SDHI-0" \
	"ch1" "SDHI-1" \
	"ch0+ch1" "SDHI-0 and SDHI-1" \
	"no" "" \
	2> /tmp/answer.txt

    # ESC was pressed?
    if [ "$?" != "0" ] ; then continue ;  fi

    hassdhi=$(cat /tmp/answer.txt)
    continue
  fi

  ####################################
  # usb0
  ####################################
  echo "$ans" | grep -q "USB ch0:"
  if [ "$?" == "0" ] ; then
    whiptail --title "USB ch0" --nocancel --menu "Does this board have USB ch0?" 0 0 0 \
	"host" "(configured to be a host)" \
	"function" "(configured to be a function )" \
	"no" "" \
	2> /tmp/answer.txt

    # ESC was pressed?
    if [ "$?" != "0" ] ; then continue ;  fi

    usb0=$(cat /tmp/answer.txt)
    continue
  fi

  ####################################
  # usb1
  ####################################
  echo "$ans" | grep -q "USB ch1:"
  if [ "$?" == "0" ] ; then
    whiptail --title "USB ch1" --nocancel --menu "Does this board have USB ch1?" 0 0 0 \
	"host" "(configured to be a host)" \
	"function" "(configured to be a function)" \
	"no" "" \
	2> /tmp/answer.txt

    # ESC was pressed?
    if [ "$?" != "0" ] ; then continue ;  fi

    usb1=$(cat /tmp/answer.txt)
    continue
  fi


  ####################################
  # haslcd
  ####################################
  echo "$ans" | grep -q "LCD:"
  if [ "$?" == "0" ] ; then

    if [ "$devicetype" != "RZ_A2M" ] ; then
      # RZ/A1
      if [ "$devicetype" == "RZ_A1L" ] ; then
        whiptail --title "LCD" --nocancel --menu "Does this board have a LCD?" 0 0 0 \
	"ch0-RGB24" "VDC5 ch0 with 24-bit parallel connection (RGB888)" \
	"ch0-RGB18" "VDC5 ch0 with 18-bit parallel connection (RGB666)" \
	"ch0-RGB16" "VDC5 ch0 with 16-bit parallel connection (RGB565)" \
	"ch0-LVDS" "VDC5 ch0 with LVDS connection" \
	"no" "" \
	2> /tmp/answer.txt
      else
        whiptail --title "LCD" --nocancel --menu "Does this board have a LCD?" 0 0 0 \
	"ch0-RGB24" "VDC5 ch0 with 24-bit parallel connection (RGB888)" \
	"ch0-RGB18" "VDC5 ch0 with 18-bit parallel connection (RGB666)" \
	"ch0-RGB16" "VDC5 ch0 with 16-bit parallel connection (RGB565)" \
	"ch0-LVDS" "VDC5 ch0 with LVDS output" \
	"ch1-RGB24" "VDC5 ch1 with 24-bit parallel connection (RGB888)" \
	"ch1-RGB18" "VDC5 ch1 with 18-bit parallel connection (RGB666)" \
	"ch1-RGB16" "VDC5 ch1 with 16-bit parallel connection (RGB565)" \
	"ch1-LVDS" "VDC5 ch1 with LVDS connection" \
	"no" "" \
	2> /tmp/answer.txt
      fi
    else
      # RZ/A2
        whiptail --title "LCD" --nocancel --menu "Does this board have a LCD?" 0 0 0 \
	"ch0-RGB24" "VDC6 with 24-bit parallel connection (RGB888)" \
	"ch0-RGB18" "VDC6 with 18-bit parallel connection (RGB666)" \
	"ch0-RGB16" "VDC6 with 16-bit parallel connection (RGB565)" \
	"ch0-LVDS" "VDC6 with LVDS connection" \
	"no" "" \
	2> /tmp/answer.txt
    fi

    # ESC was pressed?
    if [ "$?" != "0" ] ; then continue ;  fi

    haslcd=$(cat /tmp/answer.txt)

    continue
  fi

  ####################################
  # haseth
  ####################################
  echo "$ans" | grep -q "Ethernet:"
  if [ "$?" == "0" ] ; then
    if [ "$devicetype" != "RZ_A2M" ] ; then
      # RZ/A1
      whiptail --title "Ethernet" --nocancel --menu "Does this board have Ethernet?" 0 0 0 \
	"yes" "" \
	"no" "" \
	2> /tmp/answer.txt
    else
      # RZ/A2
      whiptail --title "Ethernet" --nocancel --menu "Does this board have Ethernet?" 0 0 0 \
	"ch0" "Channel 0 only" \
	"ch1" "Channel 1 only" \
	"ch0+ch1" "Channel 0 and Channel 1" \
	"no" "" \
	2> /tmp/answer.txt
    fi

    # ESC was pressed?
    if [ "$?" != "0" ] ; then continue ;  fi

    haseth=$(cat /tmp/answer.txt)
    continue
  fi

  # Create BSP
  echo "$ans" | grep -q "Create BSP"
  if [ "$?" == "0" ] ; then

    # Do you want to continue?
    whiptail --title "Create BSP" --yesno \
"We will now create the following files:\n"\
"arch/arm/configs/${boardname}_defconfig\n"\
"arch/arm/configs/${boardname}_xip_defconfig\n"\
"arch/arm/boot/dts/$DTS_FILENAME.dts\n"\
"\n"\
"Do you want to continue?\n" 0 0 2> /tmp/answer.txt

    if [ "$?" == "0" ] ; then
      # yes
      break
    fi
  fi

  # while loop
  continue

done

##########################################################################################
# Done with Menu. Now copy and modify BSP files
##########################################################################################

function remove_section {

  #   /* SECT_XXXX */
  #   blaw
  #   blaw
  #   /* SECT_XXXX_END */

  #  $1 = section name
  #  $2 = file

  START_LINE=SECT_${1}
  END_LINE=SECT_${1}_END

  # Remove lines between /* SECT_XXXX */ and /* SECT_XXXX_END */
  sed -i -e '/'" $START_LINE "'/,/'" $END_LINE "'/d' $2
}

function keep_section {
  #   /* SECT_XXXX */
  #   blaw
  #   blaw
  #   /* SECT_XXXX_END */

  #  $1 = section name
  #  $2 = file

  START_LINE=SECT_${1}
  END_LINE=SECT_${1}_END

  # Remove just line /* SECT_XXXX */
  sed -i -e '/'" $START_LINE "'/,1d' $2

  # Remove just line /* SECT_XXXX_END */
  sed -i -e '/'" $END_LINE "'/,1d' $2
}


# copy over the files
if [ "$devicetype" != "RZ_A2M" ] ; then
  # RZ/A1
  #DTS_FILENAME=r7s72100-${boardname}
  #cp -a arch/arm/configs/rza1template_defconfig arch/arm/configs/${boardname}_defconfig
  cp -a arch/arm/configs/rza1template_xip_defconfig arch/arm/configs/${boardname}_defconfig
  cp -a arch/arm/configs/rza1template_xip_defconfig arch/arm/configs/${boardname}_xip_defconfig
  #cp -a arch/arm/boot/dts/r7s72100-rza1template.dts arch/arm/boot/dts/r7s72100-${boardname}.dts
  cp -a arch/arm/boot/dts/r7s72100-rza1template.dts arch/arm/boot/dts/$DTS_FILENAME.dts
  cp -a arch/arm/mach-shmobile/board-rza1template.c arch/arm/mach-shmobile/board-${boardname}.c
else
  # RZ/A2
  #DTS_FILENAME=r7s9210-${boardname}
  #cp -a arch/arm/configs/rza2template_defconfig arch/arm/configs/${boardname}_defconfig
  cp -a arch/arm/configs/rza2template_xip_defconfig arch/arm/configs/${boardname}_defconfig
cp -a arch/arm/configs/rza2template_xip_defconfig arch/arm/configs/${boardname}_xip_defconfig
  #cp -a arch/arm/boot/dts/r7s9210-rza2template.dts arch/arm/boot/dts/r7s9210-${boardname}.dts
  cp -a arch/arm/boot/dts/r7s9210-rza2template.dts arch/arm/boot/dts/$DTS_FILENAME.dts
  cp -a arch/arm/mach-shmobile/board-rza2template.c arch/arm/mach-shmobile/board-${boardname}.c
fi

# Remove XIP settings from XIP defconfig to make it a non-xip defconfig
sed -i -e '/.*CONFIG_PHYS_OFFSET.*/,+d' arch/arm/configs/${boardname}_defconfig
sed -i -e '/.*CONFIG_XIP_KERNEL.*/,+d' arch/arm/configs/${boardname}_defconfig
sed -i -e '/.*CONFIG_XIP_PHYS_ADDR.*/,+d' arch/arm/configs/${boardname}_defconfig
sed -i -e '/.*CONFIG_AXFS.*/,+d' arch/arm/configs/${boardname}_defconfig


# Convert board name to upper case
boardnameupper=`echo ${boardname} | tr '[:lower:]' '[:upper:]'`


# use sed to change all instances of the board name to the new name
# rzatemplate >> ${boardname}
# RZATEMPLATE >> ${boardnameupper}
# companyname >> ${companyname}

# arch/arm/configs/xxxx_defconfig
sed -i "s/RZATEMPLATE/${boardnameupper}/g"	arch/arm/configs/${boardname}_defconfig

# arch/arm/configs/xxxx_xip_defconfig
sed -i "s/RZATEMPLATE/${boardnameupper}/g"	arch/arm/configs/${boardname}_xip_defconfig

# arch/arm/boot/dts/r7s72100-xxxx.dts
sed -i "s/rzatemplate/${boardname}/g"		arch/arm/boot/dts/$DTS_FILENAME.dts
sed -i "s/RZATEMPLATE/${boardnameupper}/g"	arch/arm/boot/dts/$DTS_FILENAME.dts
sed -i "s/mycompany/${companyname}/g"		arch/arm/boot/dts/$DTS_FILENAME.dts

# arch/arm/mach-shmobile/board-xxxx.c
sed -i "s/rzatemplate/${boardname}/g"		arch/arm/mach-shmobile/board-${boardname}.c
sed -i "s/RZATEMPLATE/${boardnameupper}/g"	arch/arm/mach-shmobile/board-${boardname}.c
sed -i "s/mycompany/${companyname}/g"		arch/arm/mach-shmobile/board-${boardname}.c



# NOTE: When finding the line to insert our new board after, notice the extra
#       '-' which makes the text search only return that line
LINE=---------------------------------------------


# arch/arm/boot/dts/Makefile
ALREADY_ADDED=`grep $DTS_FILENAME.dtb arch/arm/boot/dts/Makefile`
if [ "$ALREADY_ADDED" == "" ] ; then
  sed -i "s/${LINE}/${LINE}\ndtb-y += $DTS_FILENAME.dtb/g" arch/arm/boot/dts/Makefile
fi

# arch/arm/mach-shmobile/Makefile
ALREADY_ADDED=`grep CONFIG_MACH_${boardnameupper} arch/arm/mach-shmobile/Makefile`
if [ "$ALREADY_ADDED" == "" ] ; then
  sed -i "s/${LINE}/${LINE}\nobj-\$(CONFIG_MACH_${boardnameupper})	+= board-${boardname}.o/g"  arch/arm/mach-shmobile/Makefile
fi

# arch/arm/mach-shmobile/Kconfig
ALREADY_ADDED=`grep MACH_${boardnameupper} arch/arm/mach-shmobile/Kconfig`
if [ "$ALREADY_ADDED" == "" ] ; then
  sed -i "s/${LINE}/${LINE}\nconfig MACH_${boardnameupper}\n\tbool \"${boardnameupper} board\"\n/g"  arch/arm/mach-shmobile/Kconfig
fi


####################################
# Modify the device tree and board file
####################################

#devicetype
#-----------------------------
if [ "$devicetype" == "RZ_A1H" ] ; then
  echo ""  # keep default setting
elif [ "$devicetype" == "RZ_A1M" ] ; then
  echo ""  # keep default setting
elif [ "$devicetype" == "RZ_A1L" ] ; then
  # remove comments from "//#define CONFIG_RZA1L"
  sed -i 's:\/\/#define CONFIG_RZA1L:#define CONFIG_RZA1L:' arch/arm/boot/dts/$DTS_FILENAME.dts
elif [ "$devicetype" == "RZ_A2M" ] ; then
  echo ""  # keep default setting
fi


#extal
#-----------------------------
if [ "$devicetype" != "RZ_A2M" ] ; then
  # RZ/A1
  sed -i "s/13.33MHz/$extal/" arch/arm/boot/dts/$DTS_FILENAME.dts
  sed -i "s/13330000/$extalspeed/" arch/arm/boot/dts/$DTS_FILENAME.dts
else
  # RZ/A2
  sed -i "s/24MHz/$extal/" arch/arm/boot/dts/$DTS_FILENAME.dts
  sed -i "s/24000000/$extalspeed/" arch/arm/boot/dts/$DTS_FILENAME.dts
fi
if [ "$extal" == "none" ] ; then
  remove_section "EXTAL" arch/arm/boot/dts/$DTS_FILENAME.dts
else
  keep_section "EXTAL" arch/arm/boot/dts/$DTS_FILENAME.dts
fi

#hasusbxtal
#-----------------------------
if [ "$hasusbxtal" == "no" ] ; then
  remove_section "USB_EXTAL" arch/arm/boot/dts/$DTS_FILENAME.dts

  # usb nodes
  sed -i "s/xtal-48mhz/xtal-12mhz/" arch/arm/boot/dts/$DTS_FILENAME.dts
else
  keep_section "USB_EXTAL" arch/arm/boot/dts/$DTS_FILENAME.dts
fi

#hasrtcxtal
#-----------------------------
if [ "$hasrtcxtal" == "no" ] ; then
  remove_section "RTC_X1" arch/arm/boot/dts/$DTS_FILENAME.dts
fi

#scif
#-----------------------------
sed -i "s/scif2/scif${scif}/" arch/arm/boot/dts/$DTS_FILENAME.dts
sed -i "s:serial2:serial${scif}:" arch/arm/boot/dts/$DTS_FILENAME.dts
sed -i "s/\* TxD2 \*/\* TxD${scif} \*/" arch/arm/boot/dts/$DTS_FILENAME.dts
sed -i "s/\* RxD2 \*/\* RxD${scif} \*/" arch/arm/boot/dts/$DTS_FILENAME.dts

#memory
#-----------------------------
if [ "$devicetype" != "RZ_A2M" ] ; then
  # RZ/A1
  #	memory@8000000 {
  #		device_type = "memory";
  #		reg = <0x20000000 0x00A00000>;
  #	};
  sed -i "s:memory@8000000:memory@${memoryaddr}:" arch/arm/boot/dts/$DTS_FILENAME.dts
  sed -i "s:reg = <0x20000000 0x00A00000>;:reg = <0x${memoryaddr} 0x${memorysize}>;\t \/\* ${memorysizename} of ${memory} \*\/:" arch/arm/boot/dts/$DTS_FILENAME.dts
  #CONFIG_PHYS_OFFSET=0x20000000
  sed -i "s:CONFIG_PHYS_OFFSET=0x20000000:CONFIG_PHYS_OFFSET=0x${memoryaddr}:" arch/arm/configs/${boardname}_xip_defconfig
else
  # RZ/A2
  #	memory@80000000 {
  #		device_type = "memory";
  #		reg = <0x80000000 0x00400000>;
  #	};
  sed -i "s:memory@80000000:memory@${memoryaddr}:" arch/arm/boot/dts/$DTS_FILENAME.dts
  sed -i "s:reg = <0x80000000 0x00400000>;:reg = <0x${memoryaddr} 0x${memorysize}>;\t \/\* ${memorysizename} of ${memory} \*\/:" arch/arm/boot/dts/$DTS_FILENAME.dts
  #CONFIG_PHYS_OFFSET=0x80000000
  sed -i "s:CONFIG_PHYS_OFFSET=0x80000000:CONFIG_PHYS_OFFSET=0x${memoryaddr}:" arch/arm/configs/${boardname}_xip_defconfig
fi


#hasmmc
#-----------------------------
if [ "$hasmmc" == "no" ] ; then
  remove_section "MMC" arch/arm/boot/dts/$DTS_FILENAME.dts

  sed -i -e '/.*CONFIG_MMC_SH_MMCIF.*/,+d' arch/arm/configs/${boardname}_defconfig
  sed -i -e '/.*CONFIG_MMC_SH_MMCIF.*/,+d' arch/arm/configs/${boardname}_xip_defconfig
else
  keep_section "MMC" arch/arm/boot/dts/$DTS_FILENAME.dts
fi
if [ "$devicetype" == "RZ_A2M" ] ; then
  # RZ/A2
  ## MMC and SD are the same thing
  if [ "$hasmmc" == "ch0" ] ; then
    keep_section "SDHI_CH0" arch/arm/boot/dts/$DTS_FILENAME.dts
  fi
  if [ "$hasmmc" == "ch1" ] ; then
    keep_section "SDHI_CH1" arch/arm/boot/dts/$DTS_FILENAME.dts
  fi
fi


#hassdhi
#-----------------------------
if [ "$hassdhi" == "no" ] && [ "$hasmmc" == "no" ] ; then
  sed -i -e '/.*CONFIG_MMC.*/,+d' arch/arm/configs/${boardname}_defconfig
  sed -i -e '/.*CONFIG_MMC.*/,+d' arch/arm/configs/${boardname}_xip_defconfig
fi

if [ "$hassdhi" == "ch0" ] || [ "$hassdhi" == "ch0+ch1" ] ; then
  keep_section "SDHI_CH0" arch/arm/boot/dts/$DTS_FILENAME.dts
fi
if [ "$hassdhi" == "ch1" ] || [ "$hassdhi" == "ch0+ch1" ] ; then
  keep_section "SDHI_CH1" arch/arm/boot/dts/$DTS_FILENAME.dts
fi
# remove any sections that are left
remove_section "SDHI_CH0" arch/arm/boot/dts/$DTS_FILENAME.dts
remove_section "SDHI_CH1" arch/arm/boot/dts/$DTS_FILENAME.dts


#usb0 usb1
#-----------------------------
if [ "$usb1" == "host" ] ; then
  keep_section "USB_HOST_CH1" arch/arm/boot/dts/$DTS_FILENAME.dts
  remove_section "USB_FUNCTION_CH1" arch/arm/boot/dts/$DTS_FILENAME.dts

  if [ "$devicetype" != "RZ_A2M" ] ; then
    # For RZ/A1, you must enable ch0 to use ch1
    if [ "$usb0" == "no" ] ; then
      keep_section "USB_HOST_CH0" arch/arm/boot/dts/$DTS_FILENAME.dts
    fi
  fi
fi
if [ "$usb1" == "function" ] ; then
  remove_section "USB_HOST_CH1" arch/arm/boot/dts/$DTS_FILENAME.dts
  keep_section "USB_FUNCTION_CH1" arch/arm/boot/dts/$DTS_FILENAME.dts

  if [ "$devicetype" != "RZ_A2M" ] ; then
    # For RZ/A1, you must enable ch0 to use ch1
    if [ "$usb0" == "no" ] ; then
      keep_section "USB_FUNCTION_CH0" arch/arm/boot/dts/$DTS_FILENAME.dts
    fi
  fi
fi

if [ "$usb0" == "host" ] ; then
  keep_section "USB_HOST_CH0" arch/arm/boot/dts/$DTS_FILENAME.dts
fi
if [ "$usb0" == "function" ] ; then
  keep_section "USB_FUNCTION_CH0" arch/arm/boot/dts/$DTS_FILENAME.dts
fi
# Remove what is left
remove_section "USB_HOST_CH0" arch/arm/boot/dts/$DTS_FILENAME.dts
remove_section "USB_FUNCTION_CH0" arch/arm/boot/dts/$DTS_FILENAME.dts
remove_section "USB_HOST_CH1" arch/arm/boot/dts/$DTS_FILENAME.dts
remove_section "USB_FUNCTION_CH1" arch/arm/boot/dts/$DTS_FILENAME.dts


#haseth
#-----------------------------
if [ "$haseth" != "no" ] ; then
  # RZ/A1 only has 1 channel
  keep_section "ETHERNET" arch/arm/boot/dts/$DTS_FILENAME.dts
fi
if [ "$haseth" == "ch0" ] || [ "$haseth" == "ch0+ch1" ] ; then
  keep_section "ETHERNET_CH0" arch/arm/boot/dts/$DTS_FILENAME.dts
fi
if [ "$haseth" == "ch1" ] || [ "$haseth" == "ch0+ch1" ] ; then
  keep_section "ETHERNET_CH1" arch/arm/boot/dts/$DTS_FILENAME.dts
fi
#remove anything that is left
remove_section "ETHERNET" arch/arm/boot/dts/$DTS_FILENAME.dts
remove_section "ETHERNET_CH0" arch/arm/boot/dts/$DTS_FILENAME.dts
remove_section "ETHERNET_CH1" arch/arm/boot/dts/$DTS_FILENAME.dts


#haslcd
#-----------------------------

# RZ/A1
#/* SECT_VDC5_PARALLEL */
#/* SECT_VDC5_LVDS */
#/* SECT_VDC5 */

if [ "$haslcd" == "no" ] ; then
  remove_section "VDC5" arch/arm/boot/dts/$DTS_FILENAME.dts
  remove_section "VDC5_PARALLEL" arch/arm/boot/dts/$DTS_FILENAME.dts
  remove_section "VDC5_LVDS" arch/arm/boot/dts/$DTS_FILENAME.dts
else
  keep_section "VDC5" arch/arm/boot/dts/$DTS_FILENAME.dts
fi

# RGB24, RGB18, RGB16
if [ "${haslcd:4:3}" == "RGB" ] ; then
  keep_section "VDC5_PARALLEL" arch/arm/boot/dts/$DTS_FILENAME.dts
  remove_section "VDC5_LVDS" arch/arm/boot/dts/$DTS_FILENAME.dts
fi
# RGB18
if [ "${haslcd:4}" == "RGB18" ] ; then
  #out_format = <1>
  sed -i -e 's/out_format = <0>/out_format = <1>/' arch/arm/boot/dts/$DTS_FILENAME.dts

  # remove unnecessary pins
  sed -i -e '/_DATA18 /,+1d' arch/arm/boot/dts/$DTS_FILENAME.dts
  sed -i -e '/_DATA19 /,+1d' arch/arm/boot/dts/$DTS_FILENAME.dts
  sed -i -e '/_DATA20 /,+1d' arch/arm/boot/dts/$DTS_FILENAME.dts
  sed -i -e '/_DATA21 /,+1d' arch/arm/boot/dts/$DTS_FILENAME.dts
  sed -i -e '/_DATA22 /,+1d' arch/arm/boot/dts/$DTS_FILENAME.dts
  sed -i -e '/_DATA23 /,+1d' arch/arm/boot/dts/$DTS_FILENAME.dts
fi
# RGB16
if [ "${haslcd:4}" == "RGB16" ] ; then
  #out_format = <2>
  sed -i -e 's/out_format = <0>/out_format = <2>/' arch/arm/boot/dts/$DTS_FILENAME.dts

  # remove unnecessary pins
  sed -i -e '/_DATA16 /,+1d' arch/arm/boot/dts/$DTS_FILENAME.dts
  sed -i -e '/_DATA17 /,+1d' arch/arm/boot/dts/$DTS_FILENAME.dts
  sed -i -e '/_DATA18 /,+1d' arch/arm/boot/dts/$DTS_FILENAME.dts
  sed -i -e '/_DATA19 /,+1d' arch/arm/boot/dts/$DTS_FILENAME.dts
  sed -i -e '/_DATA20 /,+1d' arch/arm/boot/dts/$DTS_FILENAME.dts
  sed -i -e '/_DATA21 /,+1d' arch/arm/boot/dts/$DTS_FILENAME.dts
  sed -i -e '/_DATA22 /,+1d' arch/arm/boot/dts/$DTS_FILENAME.dts
  sed -i -e '/_DATA23 /,+1d' arch/arm/boot/dts/$DTS_FILENAME.dts
fi

# LVDS
if [ "${haslcd:4}" == "LVDS" ] ; then

  remove_section "VDC5_PARALLEL" arch/arm/boot/dts/$DTS_FILENAME.dts
  keep_section "VDC5_LVDS" arch/arm/boot/dts/$DTS_FILENAME.dts

  #use_lvds = <1>
  sed -i "s/use_lvds = <0>/use_lvds = <1>/g" arch/arm/boot/dts/$DTS_FILENAME.dts

  # panel_icksel = <0>;	/* (don't care when lvds=1) */
  sed -i -e "s/panel_icksel =.*/panel_icksel = <0>;	\/\* (don't care when lvds=1) \*\//" arch/arm/boot/dts/$DTS_FILENAME.dts

  # panel_ocksel = <2>;	/* 2=OCKSEL_PLL_DIV7 (Peripheral clock 1) */
  sed -i -e "s/panel_ocksel =.*/panel_ocksel = <2>;	\/\* 2=OCKSEL_PLL_DIV7 (LVDS PLL clock divided by 7) \*\//" arch/arm/boot/dts/$DTS_FILENAME.dts
fi

if [ "${haslcd:0:3}" == "ch1" ] ; then

  #vdc50 -> vdc51
  sed -i "s/vdc50/vdc51/g" arch/arm/boot/dts/$DTS_FILENAME.dts

  #VDC5 LCD ch 0 -> VDC5 LCD ch 1
  sed -i "s/VDC5 LCD ch 0/VDC5 LCD ch 1/g" arch/arm/boot/dts/$DTS_FILENAME.dts

  #LCD0_ -> LCD1_
  sed -i "s/LCD0_/LCD1_/g" arch/arm/boot/dts/$DTS_FILENAME.dts

fi

if [ "$memory" != "Internal RAM only" ] ; then

  # remove '//' from //#define VDC5_FB_ADDR (0x60000000)
  sed -i "s/\/\/#define VDC5_FB_ADDR /#define VDC5_FB_ADDR /g" arch/arm/boot/dts/$DTS_FILENAME.dts

  #define VDC5_FB_ADDR 0	/* 0 = allocate memory at probe (don't use when using SDRAM) */
  # ad '//' in front of "#define VDC5_FB_ADDR 0"
  sed -i "s/#define VDC5_FB_ADDR 0/\/\/#define VDC5_FB_ADDR 0/g" arch/arm/boot/dts/$DTS_FILENAME.dts

fi


###############################
# done
###############################

whiptail --msgbox \
"Complete!"\
"\nPlease note that none of the pin mux settings have been configured yet.\n"\
"You MUST manually edit the device tree file: \n"\
"      arch/arm/boot/dts/$DTS_FILENAME.dts\n\n"\
"Also, please review your configuration file and make any necessary changes:\n"\
"      include/configs/${boardname}.h\n\n"\
"To build for your board, please use the following commands:\n"\
"   make ${boardname}_defconfig\n"\
"     or\n"\
"   make ${boardname}_xip_defconfig\n"\
"   make\n\n"\
"Or, if you are using the bsp build environment:\n"\
"   ./build.sh kernel ${boardname}_xip_defconfig\n"\
"   ./build.sh kernel uImage\n"\
"     or\n"\
"   ./build.sh kernel xipImage\n" 0 0

gedit arch/arm/boot/dts/$DTS_FILENAME.dts &

