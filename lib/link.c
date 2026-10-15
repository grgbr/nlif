#include "link.h"
#include <stroll/bops.h>

ssize_t
nlif_link_validate_strid(const char * string)
{
	nlif_assert(string);

	size_t len = strnlen(string, IFNAMSIZ);

	if (!len)
		return -ENODATA;

	if (len >= IFNAMSIZ)
		return -ENAMETOOLONG;

	return (ssize_t)len;
}

#if defined(CONFIG_NLIF_PRINT)

#define NLIF_IFACE_FLAGS_STRSZ (256U)

char *
nlif_link_flags_str(unsigned int flags, char string[NLIF_IFACE_FLAGS_STRSZ])
{
	nlif_assert(string);

	if (flags) {
		unsigned int fl = stroll_bops_ffs(flags);
		const char * src = rt_link_ifinfo_flags_str(1U << (fl - 1));
		size_t       slen = strlen(src);
		size_t       dlen;

		if (slen >= NLIF_IFACE_FLAGS_STRSZ)
			goto abort;
		memcpy(string, src, slen);
		dlen = slen;
		flags >>= fl;

		while (flags) {
			unsigned int f = stroll_bops_ffs(flags);

			fl += f;
			src = rt_link_ifinfo_flags_str(1U << (fl - 1));
			slen = strlen(src);
			if ((dlen + 1 + slen) >= NLIF_IFACE_FLAGS_STRSZ)
				goto abort;
			string[dlen++] = ',';
			memcpy(&string[dlen], src, slen);
			dlen += slen;
			flags >>= f;
		}

		string[dlen] = '\0';
	}
	else
		string[0] ='\0';

	return string;

abort:
	nlif_abort("internal bug: link flag string not large enough !");
}

const char *
nlif_link_operstate_str(unsigned char operstate)
{
	switch (operstate) {
	case IF_OPER_NOTPRESENT:
		return "notpresent";
	case IF_OPER_DOWN:
		return "down";
	case IF_OPER_LOWERLAYERDOWN:
		return "lowerlayerdown";
	case IF_OPER_TESTING:
		return "testing";
	case IF_OPER_DORMANT:
		return "dormant";
	case IF_OPER_UP:
		return "up";
	case IF_OPER_UNKNOWN:
		return "unknown";
	default:
		nlif_assert(0);
	}

	unreachable();
}

const char *
nlif_link_mode_str(unsigned char mode)
{
	switch (mode) {
	case IF_LINK_MODE_DEFAULT:
		return "default";
	case IF_LINK_MODE_DORMANT:
		return "dormant";
	case IF_LINK_MODE_TESTING:
		return "testing";
	default:
		nlif_assert(0);
	}

	unreachable();
}

const char *
nlif_link_type_str(unsigned short type)
{
	switch (type) {
	case ARPHRD_ETHER:
		return "ethernet";
	case ARPHRD_LOOPBACK:
		return "loopback";
	case ARPHRD_VOID:
		return "unknown";
	default:
		return "unsupported";
	}
}

void
nlif_link_print(const struct rt_link_getlink_rsp * link, FILE * stdio)
{
	nlif_link_assert(link);
	nlif_assert(stdio);

	const struct ifinfomsg * info = &link->_hdr;
	char *                   str;

	str = nlif_malloc(stroll_max(stroll_max(NLIF_LINK_FLAGS_STRSZ,
	                                        NLIF_LINK_HWADDR_STRSZ),
	                             NLIF_UINT_STRSZ));
	nlif_assert(str);

	fprintf(stdio, "%3d: %s\n", info->ifi_index, link->ifname);

	fprintf(stdio, "     group:     %u\n", link->group);

	fprintf(stdio,
	        "     flags:     %s\n",
	        nlif_link_flags_str(info->ifi_flags, str));

	fprintf(stdio,
	        "     change:    %s\n",
	        nlif_link_flags_str(info->ifi_flags, str));

	fprintf(stdio,
	        "     operstate: %s\n",
	        nlif_link_operstate_str(link->operstate));

	fprintf(stdio, "     carrier:   %u\n", link->carrier);

	fprintf(stdio,
	        "     linkmode:  %s\n",
	        nlif_link_mode_str(link->linkmode));

	fprintf(stdio,
	        "     type:      %s\n",
	        nlif_link_type_str(info->ifi_type));

	fprintf(stdio,
	        "     kind:      %s\n",
	        nlif_link_kind_str(((link->_present.linkinfo &&
	                             link->linkinfo._len.kind))
	                           ? link->linkinfo.kind
	                           : NULL));

	fprintf(stdio,
	        "     alias:     %s\n",
	        nlif_link_alias_str(link->_len.ifalias ? link->ifalias : NULL));

	if (link->prop_list._count.alt_ifname) {
		unsigned int a;

		fprintf(stdio,
		        "     altnames:  %s",
		        link->prop_list.alt_ifname[0]->str);
		for (a = 1; a < link->prop_list._count.alt_ifname; a++)
			fprintf(stdio,
			        ",%s",
			        link->prop_list.alt_ifname[a]->str);
		fputc('\n', stdio);
	}
	else
		fprintf(stdio, "     altnames:  none\n");

	fprintf(stdio,
	        "     link:      %s\n",
	        nlif_link_link_str(link->link, str));

	fprintf(stdio,
	        "     master:    %s\n",
	        nlif_link_master_str(link->master, str));
	

	fprintf(stdio,
	        "     mtu:       %s\n",
	        nlif_link_mtu_str(link->mtu, str));

	fprintf(stdio,
	        "     hwaddr:    %s\n",
	        nlif_link_hwaddr_str((const struct ether_addr *)link->address, str));

	nlif_free(str);
}

#endif /* defined(CONFIG_NLIF_PRINT) */
