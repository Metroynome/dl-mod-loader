/* Bounded, allocation-free INI persistence. Temporary values are committed
 * only after the complete file validates. File writes handle short writes. */
#include "config.h"

#define io       (moduleConfigApi())
#define inConfig (*moduleConfigBusy())
#ifndef CONFIG_WRITE
#define CONFIG_WRITE configWrite
#define CONFIG_DEFAULT_WRITE
#endif

static int same(const char* a, const char* b)
{
	while (*a && *a == *b) {
		++a;
		++b;
	}
	return *a == *b;
}

static int identifier(const char* s, int max)
{
	int n = 0;
	u8 c;
	if (!s || !*s)
		return 0;
	while ((c = *s++)) {
		if (++n > max ||
		    !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
			return 0;
	}
	return 1;
}

static char* append(char* p, const char* s)
{
	while (*s)
		*p++ = *s++;
	*p = 0;
	return p;
}

static int prepare(const char* mod, const moduleConfigField_t* f, int n, char* path, int saving)
{
	int i, j;
	char* p;
	const char* region;
	if (!io || !identifier(mod, 48) || !f || n < 1 || n > DL_CONFIG_MAX_FIELDS)
		return 0;
	region = io->region == 1 ? "ntscu" : io->region == 2 ? "pal" : io->region == 3 ? "ntscj" : io->region == 4 ? "ntsck" : 0;
	if (!region)
		return 0;
	for (i = 0; i < n; ++i) {
		if (!identifier(f[i].key, 31) || !f[i].value || f[i].minimum > f[i].maximum)
			return 0;
		if (saving && (*f[i].value < f[i].minimum || *f[i].value > f[i].maximum))
			return 0;
		for (j = 0; j < i; ++j)
			if (same(f[j].key, f[i].key))
				return 0;
	}
	p = append(path, "host0:dl/config/");
	p = append(p, mod);
	*p++ = '.';
	p = append(p, region);
	append(p, ".ini");
	return 1;
}

static int space(char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

static char* trim(char* s)
{
	char* end = s;
	while (*end)
		++end;
	while (end > s && space(end[-1]))
		*--end = 0;
	while (space(*s))
		++s;
	return s;
}

static int integer(const char* s, int* value)
{
	u32 v = 0, limit = 0x7fffffff, d;
	int negative = 0;
	if (*s == '-' || *s == '+') {
		negative = *s == '-';
		++s;
	}
	if (negative)
		++limit;
	if (*s < '0' || *s > '9')
		return 0;
	while (*s >= '0' && *s <= '9') {
		d = *s++ - '0';
		if (v > limit / 10 || (v == limit / 10 && d > limit % 10))
			return 0;
		v = v * 10 + d;
	}
	if (*s)
		return 0;
	*value = (int)(negative ? 0 - v : v);
	return 1;
}

static int parse(char* text, const moduleConfigField_t* f, int n)
{
	int values[DL_CONFIG_MAX_FIELDS], i;
	u32 seen = 0;
	char *line = text, *next, *key, *value, *p;
	for (i = 0; i < n; ++i)
		values[i] = *f[i].value;
	while (*line) {
		next = line;
		while (*next && *next != '\n')
			++next;
		if (*next)
			*next++ = 0;
		for (p = line; *p; ++p)
			if (*p == ';' || *p == '#') {
				*p = 0;
				break;
			}
		key = trim(line);
		if (*key && !same(key, "[settings]")) {
			value = key;
			while (*value && *value != '=')
				++value;
			if (!*value)
				return DL_CONFIG_INVALID;
			*value++ = 0;
			key = trim(key);
			value = trim(value);
			if (!identifier(key, 31))
				return DL_CONFIG_INVALID;
			for (i = 0; i < n; ++i)
				if (same(key, f[i].key)) {
					if ((seen & (((u32)1) << i)) || !integer(value, &values[i]) || values[i] < f[i].minimum ||
					    values[i] > f[i].maximum)
						return DL_CONFIG_INVALID;
					seen |= ((u32)1) << i;
					break;
				}
		}
		line = next;
	}
	for (i = 0; i < n; ++i)
		*f[i].value = values[i];
	return DL_CONFIG_OK;
}

int moduleConfigLoad(const char* mod, const moduleConfigField_t* f, int n)
{
	char path[96], text[2048], extra;
	int fd, got, size = 0, result = DL_CONFIG_IO, i;
	if (inConfig || !prepare(mod, f, n, path, 0))
		return DL_CONFIG_INVALID;
	inConfig = 1;
	fd = io->open(path, 1, 0);
	if (fd < 0) {
		inConfig = 0;
		return fd == -2 ? DL_CONFIG_MISSING : DL_CONFIG_IO;
	}
	do {
		got = io->read(fd, text + size, 2047 - size);
		if (got < 0 || got > 2047 - size)
			goto done;
		size += got;
	} while (got && size < 2047);
	if (size == 2047 && io->read(fd, &extra, 1) != 0)
		goto done;
	result = DL_CONFIG_INVALID;
	for (i = 0; i < size; ++i)
		if (!text[i])
			goto done;
	text[size] = 0;
	/* Close succeeds before committing parsed values. */
	if (io->close(fd) < 0) {
		inConfig = 0;
		return DL_CONFIG_IO;
	}
	result = parse(text, f, n);
	inConfig = 0;
	return result;
done:
	if (io->close(fd) < 0)
		result = DL_CONFIG_IO;
	inConfig = 0;
	return result;
}

#ifdef CONFIG_DEFAULT_WRITE
static int configWrite(int fd, const void* data, int size)
{
	if (*(volatile u32*)0x12bbf0 != 0x27bdff20)
		return -1;
	return ((int (*)(int, const void*, int))0x12bbf0)(fd, data, size);
}
#endif

int moduleConfigSave(const char* mod, const moduleConfigField_t* f, int n)
{
	char path[96], text[2048], digits[11], *p;
	u32 v;
	int i, j, fd, size, written = 0, result = DL_CONFIG_OK;
	if (inConfig || !prepare(mod, f, n, path, 1))
		return DL_CONFIG_INVALID;
	p = append(text, "[settings]\n");
	for (i = 0; i < n; ++i) {
		p = append(p, f[i].key);
		*p++ = '=';
		v = (u32)*f[i].value;
		if (*f[i].value < 0) {
			*p++ = '-';
			v = 0 - v;
		}
		j = 0;
		do {
			digits[j++] = '0' + v % 10;
			v /= 10;
		} while (v);
		while (j)
			*p++ = digits[--j];
		*p++ = '\n';
	}
	size = p - text;
	inConfig = 1;
	/* Validate everything before truncating. Saves replace this mod's file.
     * The game SDK has no portable atomic rename; callers must check status. */
	fd = io->open(path, 0x602, 0666);
	if (fd < 0) {
		inConfig = 0;
		return DL_CONFIG_IO;
	}
	while (written < size) {
		i = CONFIG_WRITE(fd, text + written, size - written);
		if (i <= 0 || i > size - written) {
			result = DL_CONFIG_IO;
			break;
		}
		written += i;
	}
	if (io->close(fd) < 0)
		result = DL_CONFIG_IO;
	inConfig = 0;
	return result;
}
