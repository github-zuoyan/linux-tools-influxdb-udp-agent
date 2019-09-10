PLATFORM = $(shell ${CC} -dumpmachine)
BINARY = influxdb_agent.${PLATFORM}

SOURCES = \
	main.c \
	agent.c \
	event.c \
	influxdb.c \

CFLAGS += \
	-Wall \
	-Wextra \
	-Werror \
	-O3 \
	-g \
	-D_GNU_SOURCE \
	-std=gnu99  \

all: ${BINARY}

run: ${BINARY}
	./$< -p 8888 localhost

${BINARY}: ${SOURCES:.c=.o}
	${LINK.c} -o $@ ${LDFLAGS} $^

clean:
	@-rm ${BINARY} ${SOURCES:.c=.o}
