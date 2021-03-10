/*
 * Copyright (C) 2012 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/*
 * Oracle GPL Disclaimer: For the avoidance of doubt, except that if any license choice
 * other than GPL or LGPL is available it will apply instead, Oracle elects to use only
 * the General Public License version 2 (GPLv2) at this time for any software where
 * a choice of GPL license versions is made available with the language indicating
 * that GPLv2 or any later version may be used, or where a choice of which version
 * of the GPL is applied is otherwise unspecified.
 */

FILE_LICENCE ( GPL2_OR_LATER );

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ipxe/mii.h>

/** @file
 *
 * Media Independent Interface
 *
 */

/**
 * Reset MII interface
 *
 * @v mii		MII interface
 * @ret rc		Return status code
 */
int mii_reset ( struct mii_interface *mii ) {
	unsigned int i;
	int bmcr;
	int rc;

	/* Power-up, enable autonegotiation and initiate reset */
	if ( ( rc = mii_write ( mii, MII_BMCR,
				( BMCR_RESET | BMCR_ANENABLE ) ) ) != 0 ) {
		DBGC ( mii, "MII %p could not write BMCR: %s\n",
		       mii, strerror ( rc ) );
		return rc;
	}

	/* Wait for reset to complete */
	for ( i = 0 ; i < MII_RESET_MAX_WAIT_MS ; i++ ) {

		/* Check if reset has completed */
		bmcr = mii_read ( mii, MII_BMCR );
		if ( bmcr < 0 ) {
			rc = bmcr;
			DBGC ( mii, "MII %p could not read BMCR: %s\n",
			       mii, strerror ( rc ) );
			return rc;
		}

		/* If reset is not complete, delay 1ms and retry */
		if ( bmcr & BMCR_RESET ) {
			mdelay ( 1 );
			continue;
		}

		/* Force autonegotation on again, in case it was
		 * cleared by the reset.
		 */
		if ( ( rc = mii_write ( mii, MII_BMCR, BMCR_ANENABLE ) ) != 0 ){
			DBGC ( mii, "MII %p could not write BMCR: %s\n",
			       mii, strerror ( rc ) );
			return rc;
		}

		DBGC ( mii, "MII %p reset after %dms\n", mii, i );
		return 0;
	}

	DBGC ( mii, "MII %p timed out waiting for reset\n", mii );
	return -ETIMEDOUT;
}
