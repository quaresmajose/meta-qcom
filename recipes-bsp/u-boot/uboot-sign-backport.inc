# The three uboot-sign changes the SPL FIT boot flow needs are only available
# on the oe-core master side and are not going to be backported to wrynose, so
# they have to be reflected here:
#
#   uboot-sign: allow customizing the FIT configuration description
#   uboot-sign: list the TEE loadable behind U-Boot
#   uboot-sign: keep the SPL BSS padding when reassembling the SPL binary
#
# Both functions below are copies of the oe-core ones carrying those changes.

UBOOT_FIT_CONF_DESC ?= "Boot with signed U-Boot FIT"
SPL_WITH_DTB_BINARY ?= "u-boot-spl-dtb.bin"

concat_spl_dtb() {
	if [ -e "${SPL_DIR}/${SPL_NODTB_BINARY}" ] && [ -e "${SPL_DIR}/${SPL_DTB_BINARY}" ] ; then
		# U-Boot pads behind BSS before appending the device tree
		# ($(SPL_BIN)-pad.bin in scripts/Makefile.xpl); keep that offset.
		nodtb_size=$(stat -Lc %s ${SPL_DIR}/${SPL_NODTB_BINARY})
		dtb_offset=${nodtb_size}

		if [ -e "${SPL_DIR}/${SPL_WITH_DTB_BINARY}" ]; then
			image_size=$(stat -Lc %s ${SPL_DIR}/${SPL_WITH_DTB_BINARY})
			dtb_size=$(stat -Lc %s ${SPL_DIR}/${SPL_DTB_BINARY})
			offset=$(expr ${image_size} - ${dtb_size})
			if [ ${offset} -gt ${nodtb_size} ]; then
				dtb_offset=${offset}
			fi
		fi

		cp ${SPL_DIR}/${SPL_NODTB_BINARY} "${SPL_BINARY}"
		truncate -s ${dtb_offset} "${SPL_BINARY}"
		cat ${SPL_DIR}/${SPL_DTB_SIGNED} >> "${SPL_BINARY}"
	else
		bbwarn "Failure while adding public key to spl binary. Verified U-Boot boot won't be available."
	fi
}

uboot_fitimage_assemble() {
	conf_loadables="\"uboot\""
	conf_firmware=""
	rm -f ${UBOOT_ITS} ${UBOOT_FITIMAGE_BINARY}

	# First we create the ITS script
	cat << EOF >> ${UBOOT_ITS}
/dts-v1/;

/ {
    description = "${UBOOT_FIT_DESC}";
    #address-cells = <${UBOOT_FIT_ADDRESS_CELLS}>;

    images {
        uboot {
            description = "U-Boot image";
            data = /incbin/("${UBOOT_NODTB_BINARY}");
            type = "standalone";
            os = "u-boot";
            arch = "${UBOOT_ARCH}";
            compression = "none";
            load = <${UBOOT_FIT_UBOOT_LOADADDRESS}>;
            entry = <${UBOOT_FIT_UBOOT_ENTRYPOINT}>;
EOF

	if [ "${SPL_SIGN_ENABLE}" = "1" ] ; then
		cat << EOF >> ${UBOOT_ITS}
            signature {
                algo = "${UBOOT_FIT_HASH_ALG},${UBOOT_FIT_SIGN_ALG}";
                key-name-hint = "${SPL_SIGN_KEYNAME}";
            };
EOF
	fi

	cat << EOF >> ${UBOOT_ITS}
        };
        fdt {
            description = "U-Boot FDT";
            data = /incbin/("${UBOOT_DTB_BINARY}");
            type = "flat_dt";
            arch = "${UBOOT_ARCH}";
            compression = "none";
EOF

	if [ "${SPL_SIGN_ENABLE}" = "1" ] ; then
		cat << EOF >> ${UBOOT_ITS}
            signature {
                algo = "${UBOOT_FIT_HASH_ALG},${UBOOT_FIT_SIGN_ALG}";
                key-name-hint = "${SPL_SIGN_KEYNAME}";
            };
EOF
	fi

	cat << EOF >> ${UBOOT_ITS}
        };
EOF
	if [ "${UBOOT_FIT_TEE}" = "1" ] ; then
		conf_loadables="${conf_loadables}, \"tee\""
		uboot_fitimage_tee
	fi

	if [ "${UBOOT_FIT_ARM_TRUSTED_FIRMWARE}" = "1" ] ; then
		conf_loadables="\"atf\", ${conf_loadables}"
		uboot_fitimage_atf
	fi

	if [ -n "${UBOOT_FIT_USER_SETTINGS}" ] ; then
		printf "%b" "${UBOOT_FIT_USER_SETTINGS}" >> ${UBOOT_ITS}
	fi

	if [ -n "${UBOOT_FIT_CONF_USER_LOADABLES}" ] ; then
		conf_loadables="${conf_loadables}${UBOOT_FIT_CONF_USER_LOADABLES}"
	fi

	if [ -n "${UBOOT_FIT_CONF_FIRMWARE}" ] ; then
		conf_firmware="firmware = \"${UBOOT_FIT_CONF_FIRMWARE}\";"
	fi

	cat << EOF >> ${UBOOT_ITS}
    };

    configurations {
        default = "conf";
        conf {
            description = "${UBOOT_FIT_CONF_DESC}";
            ${conf_firmware}
            loadables = ${conf_loadables};
            fdt = "fdt";
        };
    };
};
EOF

	#
	# Assemble the U-boot FIT image
	#
	${UBOOT_MKIMAGE} \
		${@'-D "${SPL_MKIMAGE_DTCOPTS}"' if len('${SPL_MKIMAGE_DTCOPTS}') else ''} \
		-f ${UBOOT_ITS} \
		${UBOOT_FITIMAGE_BINARY}

	if [ "${SPL_SIGN_ENABLE}" = "1" ] ; then
		if [ -n "${SPL_DTB_BINARY}" ] ; then
			#
			# Sign the U-boot FIT image and add public key to SPL dtb
			#
			${UBOOT_MKIMAGE_SIGN} \
				${@'-D "${SPL_MKIMAGE_DTCOPTS}"' if len('${SPL_MKIMAGE_DTCOPTS}') else ''} \
				-F -k "${SPL_SIGN_KEYDIR}" \
				-K "${SPL_DIR}/${SPL_DTB_BINARY}" \
				-r ${UBOOT_FITIMAGE_BINARY} \
				${SPL_MKIMAGE_SIGN_ARGS}

			# Verify the U-boot FIT image and SPL dtb
			${UBOOT_FIT_CHECK_SIGN} \
				-k "${SPL_DIR}/${SPL_DTB_BINARY}" \
				-f ${UBOOT_FITIMAGE_BINARY}

			cp ${SPL_DIR}/${SPL_DTB_BINARY} ${SPL_DIR}/${SPL_DTB_SIGNED}
		else
			# Sign the U-boot FIT image
			${UBOOT_MKIMAGE_SIGN} \
				${@'-D "${SPL_MKIMAGE_DTCOPTS}"' if len('${SPL_MKIMAGE_DTCOPTS}') else ''} \
				-F -k "${SPL_SIGN_KEYDIR}" \
				-r ${UBOOT_FITIMAGE_BINARY} \
				${SPL_MKIMAGE_SIGN_ARGS}
		fi
	fi
}
