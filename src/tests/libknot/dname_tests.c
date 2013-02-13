/*  Copyright (C) 2011 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "tests/libknot/dname_tests.h"
#include "libknot/dname.h"

/* Test dname_parse_from_wire */
static int test_fw(size_t l, const char *w) {
	size_t p = 0;
	knot_dname_t *d = NULL;
	d = knot_dname_parse_from_wire((const uint8_t*)w, &p, l, NULL, NULL);
	int ret = (d != NULL);
//	d = knot_dname_new_from_wire((const uint8_t*)w, l, 0);
//	if (d) {
//		for(unsigned i = 0; i < d->label_count; ++i) {
//			diag("%d", knot_dname_label_size(d, i));
//		}
//	}
	knot_dname_free(&d);
	return ret;
}

static int dname_tests_count(int argc, char *argv[]);
static int dname_tests_run(int argc, char *argv[]);

unit_api dname_tests_api = {
	"dname",
	&dname_tests_count,
	&dname_tests_run
};

static int dname_tests_count(int argc, char *argv[])
{
	return 8;
}

static int dname_tests_run(int argc, char *argv[])
{
	const char *w = NULL;
	
	/* 1. NULL wire */
	ok(!test_fw(0, NULL), "parsing NULL dname");
	
	/* 2. empty label */
	ok(test_fw(1, ""), "parsing empty dname");
	
	/* 3. incomplete dname */
	ok(!test_fw(5, "\x08""dddd"), "parsing incomplete wire");

	/* 4. non-fqdn */
	ok(!test_fw(3, "\x02""ab"), "parsing non-fqdn name");
	
	/* 5. label > 63b */
	w = "\x40""dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
	ok(!test_fw(65, w), "parsing label > 63b");
	
	/* 6. label count > 127 */
	w = "\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64\x01\x64";
	ok(!test_fw(257, w), "parsing label count > 127");
	
	/* 7. dname length > 255 */
	w = "\xff""ddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
	ok(!test_fw(257, w), "parsing dname len > 255");
	
	/* 8. special case - invalid label */
	w = "\x20\x68\x6d\x6e\x63\x62\x67\x61\x61\x61\x61\x65\x72\x6b\x30\x30\x30\x30\x64\x6c\x61\x61\x61\x61\x61\x61\x61\x61\x62\x65\x6a\x61\x6d\x20\x67\x6e\x69\x64\x68\x62\x61\x61\x61\x61\x65\x6c\x64\x30\x30\x30\x30\x64\x6c\x61\x61\x61\x61\x61\x61\x61\x61\x62\x65\x6a\x61\x6d\x20\x61\x63\x6f\x63\x64\x62\x61\x61\x61\x61\x65\x6b\x72\x30\x30\x30\x30\x64\x6c\x61\x61\x61\x61\x61\x61\x61\x61\x62\x65\x6a\x61\x6d\x20\x69\x62\x63\x6d\x6a\x6f\x61\x61\x61\x61\x65\x72\x6a\x30\x30\x30\x30\x64\x6c\x61\x61\x61\x61\x61\x61\x61\x61\x62\x65\x6a\x61\x6d\x20\x6f\x6c\x6e\x6c\x67\x68\x61\x61\x61\x61\x65\x73\x72\x30\x30\x30\x30\x64\x6c\x61\x61\x61\x61\x61\x61\x61\x61\x62\x65\x6a\x61\x6d\x20\x6a\x6b\x64\x66\x66\x67\x61\x61\x61\x61\x65\x6c\x68\x30\x30\x30\x30\x64\x6c\x61\x61\x61\x61\x61\x61\x61\x61\x62\x65\x6a\x61\x6d\x20\x67\x67\x6c\x70\x70\x61\x61\x61\x61\x61\x65\x73\x72\x30\x30\x30\x30\x64\x6c\x61\x61\x61\x61\x61\x61\x61\x61\x62\x65\x6a\x61\x6d\x20\x65\x6b\x6c\x67\x70\x66\x61\x61\x61\x61\x65\x6c\x68\x30\x30\x30\x30\x64\x6c\x61\x61\x61\x61\x61\x0\x21\x42\x63\x84\xa5\xc6\xe7\x8\xa\xd\x11\x73\x3\x6e\x69\x63\x2\x43\x5a";
	ok(!test_fw(277, w), "parsing invalid label (spec. case 1)");

	return 0;
}
