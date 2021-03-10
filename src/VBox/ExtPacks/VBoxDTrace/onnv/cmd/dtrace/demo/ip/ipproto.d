/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma D option quiet

dtrace:::BEGIN
{
	printf("Tracing... Hit Ctrl-C to end.\n");
}

ip:::send,
ip:::receive
{
	this->protostr = args[2]->ip_ver == 4 ?
	    args[4]->ipv4_protostr : args[5]->ipv6_nextstr;
	@num[args[2]->ip_saddr, args[2]->ip_daddr, this->protostr] = count();
}

dtrace:::END
{
	printf("   %-28s %-28s %6s %8s\n", "SADDR", "DADDR", "PROTO", "COUNT");
	printa("   %-28s %-28s %6s %@8d\n", @num);
}
