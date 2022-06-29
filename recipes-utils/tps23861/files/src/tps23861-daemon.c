/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <strings.h>	/* for strcasecmp() */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <json-c/json.h>

#define TPS23861_I2C_ADDR	0x28 /* tps23861 address on i2c bus */

/* TPS23861 registers */
#define POWER_STATUS_REG	0x10 /* Power status register */
#define PT_STATUS_BASE		0x0C /* Detection & Class status */
#define PT_MODE_REG		0x12 /* Ports operating mode */
#define PT_DISCON_EN_REG	0x13 /* Auto disconnect port in low current */
#define PT_DET_CLAS_EN_REG	0x14 /* Detect and class enable */
#define PT_POWER_EN_REG		0x19 /* Ports power control */
#define CLASS_REG		0x21 /* Two Event Classification register */
#define ICUT21_CONFIG		0x2A /* Config ports 1 and 2 output power */
#define ICUT43_CONFIG		0x2B /* Config ports 3 and 4 output power */
#define PoEP_REG		0x40 /* PoE Plus register */

#define PORT_BIT( bit_i ) (0x1 << (bit_i))

/* The port power can be in any of these modes */
#define PORT_POWER_MODE_OFF	0 /* Force power off */
#define PORT_POWER_MODE_ON	1 /* Force power on */
#define PORT_POWER_MODE_AUTO	3 /* The port is powered on if connection is detected */

/* The port connection status */
#define PORT_STATE_DISCONNECTED	0 /* Nothing is plugged into the port */
#define PORT_STATE_CONNECTED	1 /* Connection detected */
#define PORT_STATE_FAULTED	2 /* Connection is faulted somehow */
#define PORT_STATE_UNKNOWN	3 /* Connection cannot be probed */

struct tps23861_config
{
	int port_mode[4];
	int port_state[4];
	int port_power[4];
	int port_autodisc;
};

static int
i2c_smbus_access(int file, char read_write, uint8_t command,
		 int size, union i2c_smbus_data *data)
{
	struct i2c_smbus_ioctl_data args;
	int err;

	memset(&args, 0, sizeof(args));
	args.read_write = read_write;
	args.command = command;
	args.size = size;
	args.data = data;

	err = ioctl(file, I2C_SMBUS, &args);
	if (err == -1)
		err = -errno;
	return err;
}

static int
i2c_smbus_write_byte_data(int file, uint8_t command, uint8_t value)
{
	union i2c_smbus_data data;

	memset(&data, 0, sizeof(data));
	data.byte = value;

	return i2c_smbus_access(file, I2C_SMBUS_WRITE, command,
				I2C_SMBUS_BYTE_DATA, &data);
}

static int
i2c_smbus_read_byte_data(int file, uint8_t command)
{
	union i2c_smbus_data data;
	int err;

	err = i2c_smbus_access(file, I2C_SMBUS_READ, command,
			       I2C_SMBUS_BYTE_DATA, &data);
	if (err < 0)
		return err;

	/* Do not allow sign propagation */
	return 0x0FFu & data.byte;
}

int open_i2c_dev(int i2cbus)
{
	char filename[20];
	int file;

	snprintf(filename, sizeof(filename), "/dev/i2c/%d", i2cbus);
	file = open(filename, O_RDWR);

	if (file < 0 && (errno == ENOENT || errno == ENOTDIR)) {
		snprintf(filename, sizeof(filename), "/dev/i2c-%d", i2cbus);
		file = open(filename, O_RDWR);
	}

	if (file < 0) {
		if (errno == ENOENT) {
			syslog(LOG_ERR, "Error: could not open file "
				"`/dev/i2c-%d' or `/dev/i2c/%d': %s\n",
				i2cbus, i2cbus, strerror(ENOENT));
		} else {
			syslog(LOG_ERR, "Error: could not open file "
				"`%s': %s\n", filename, strerror(errno));
			if (errno == EACCES)
				syslog(LOG_ERR, "Run as root?\n");
		}
	}

	return file;
}

static int
i2c_smbus_set_slave_addr(int file, int address, int force)
{
	/* With force, let the user read from/write to the registers
	   even when a driver is also running */
	if (ioctl(file, force ? I2C_SLAVE_FORCE : I2C_SLAVE, address) < 0) {
		syslog(LOG_ERR,
			"Error: could not set address to 0x%02x: %s\n",
			address, strerror(errno));
		return -errno;
	}

	return 0;
}

static int
tps23861_write_byte(int file, uint8_t command, uint8_t data)
{
	int res;

	res = i2c_smbus_write_byte_data(file, command, data);
	if (res < 0)
		syslog(LOG_ERR, "Failed to write to register 0x%x: %s\n",
		    command, strerror(-res));
	return res;
}

static int
tps23861_read_byte(int file, uint8_t command)
{
	int res;

	res = i2c_smbus_read_byte_data(file, command);
	if (res < 0)
		syslog(LOG_ERR, "Failed to read register 0x%x: %s\n",
		    command, strerror(-res));
	return res;
}

static int
tps23861_probe(int address)
{
	int res, i2cbus, file;
	int force = 0;

	/* Identify location of the controller on i2cbus 0 or 1 */
	for (i2cbus = 0; i2cbus < 2; i2cbus++)
	{
		/* Check if we can find current bus */
		file = open_i2c_dev(i2cbus);
		if (file < 0) /* Unable to access this bus: try next one */
			continue;

		/* Check if we can address the device */
		if (i2c_smbus_set_slave_addr(file, address, force) != 0) {
			close(file);
			file = -1;
			continue;
		}


		/* check if POWER_STATUS_REG address is available */
		res = i2c_smbus_read_byte_data(file, POWER_STATUS_REG);
		if (res >= 0)
			break;

		close(file);
		file = -1;
	}

	if (i2cbus >= 2) {
		syslog(LOG_INFO, "Unable to locate tps23861 conrtoller\n");
		syslog(LOG_INFO, "Assuming secondary board\n");
		return -1;
	}

	syslog(LOG_INFO, "This is primary board. TPS23861 found on i2cbus %d\n",
	    i2cbus);

	return file;
}

static int
tps23861_setup(int file)
{
	int res;

	/* Configure registers Class for Two events */
	res = tps23861_write_byte(file, PoEP_REG, 0xe0);
	if (res < 0)
		return res;

	/* Configure PoE Plus register */
	res = tps23861_write_byte(file, CLASS_REG, 0xfc);
	if (res < 0)
		return res;

	/* Config port 2 power output as 592mA x 48V = 28W */
	res = tps23861_write_byte(file, ICUT21_CONFIG, 0x60);
	if (res < 0)
		return res;

	/* Config ports 3 and 4 power output as 28W */
	res = tps23861_write_byte(file, ICUT43_CONFIG, 0x66);
	if (res < 0)
		return res;

	/*
	 * Config port 0 as automode and 1,2 and 3 as manualmode
	 */
	res = tps23861_write_byte(file, PT_MODE_REG, 0x57);
	if (res < 0)
		return res;
	/*
	 * Specification calls for 1.2ms delay  after this register
	 * is written before Detect/Class Enable (0x14) write command.
	 * Be on the save side and sleep always.
	 */
	usleep(3000);

	return 0;
}

static int
tps23861_get_port_state(int file, int port)
{
	int value;

	/*
	 * Daemon watches DETECT pn[3:0]
	 * 0x3 ---> Resistance too low (hints USB has connected between
	 *          Primary and Secondary). Ready to send power to Secondary
	 *          board.
	 * 0x6 ---> Open circuit (hints USB has NO connection between
	 *          Primary and Secondary).
	 */
	value = tps23861_read_byte(file, PT_STATUS_BASE + port);
	if (value < 0)
		return PORT_STATE_UNKNOWN;

	switch (value & 0x07) {
	case 0x03:
		return PORT_STATE_CONNECTED;
	case 0x01: /*
		    * Short circuit, happens when port connected and
		    * disconnected rapidly.
		    */
		return PORT_STATE_FAULTED;
	case 0x06:
	default:
		return PORT_STATE_DISCONNECTED;

	}
}

static void
tps23861_poll(int file, struct tps23861_config *cfg)
{
	int power_status, power_needed, power_mask, power_trans;
	int i, port_state, res, autodisc;

	/* Remember autodiscovery mask */
	autodisc = cfg->port_autodisc;

	/* In manual mode, need to re-enable port bits */
	res = tps23861_write_byte(file, PT_DET_CLAS_EN_REG, autodisc);
	if (res < 0)
		return;

	/* Read the current power state of all ports */
	power_mask = tps23861_read_byte(file, POWER_STATUS_REG);
	if (power_status < 0)
		return;

	/* By default, no power transitions */
	power_trans =  0;

	/* Check Port Status at 0xd, 0xe and 0xf */
	for (i = 1; i < 4; i++) {
		/* Check for current power status */
		power_status = (power_mask & PORT_BIT(i)) ? 1 : 0;

		power_needed = 0;

		if (cfg->port_mode[i] == PORT_POWER_MODE_ON)
			power_needed = 1;
		else if (cfg->port_mode[i] == PORT_POWER_MODE_OFF)
			power_needed = 0;
		else {
			port_state = tps23861_get_port_state(file, i);

			/* Report port connection changes in the log */
			if (port_state != cfg->port_state[i] &&
			    port_state != PORT_STATE_UNKNOWN) {
				syslog(LOG_NOTICE, "Port %d is %s\n",
				    i, port_state == PORT_STATE_CONNECTED ?
				    "connected" :
				    port_state == PORT_STATE_FAULTED ?
				       "faulted" : "disconnected");
				cfg->port_state[i] = port_state;
			}

			/*
			 * Determine what to do with the power based on
			 * the port state.
			 */
			switch (port_state) {
			case PORT_STATE_DISCONNECTED:
				power_needed = 0;
				break;
			case PORT_STATE_CONNECTED:
				power_needed = 1;
				break;
			case PORT_STATE_FAULTED:
				power_needed = 0;
				break;
			case PORT_STATE_UNKNOWN:
			default:
				/*
				 * No change to power if we do not know what
				 * state it is in.
				 */
				power_needed = power_status;
				break;
			}
		}

		if (cfg->port_power[i] != power_needed) {
			syslog(LOG_NOTICE, "Port %d power is %s\n", i,
			    power_needed ? "ON" : "OFF");
			cfg->port_power[i] = power_needed;
		}

		/* No change in requirements detected, off to next port */
		if (power_needed == power_status)
			continue;

		if (power_needed) {
			/* Switch the power on for the port */
			power_trans |= PORT_BIT(i);
		} else if (cfg->port_mode[i] == PORT_POWER_MODE_AUTO) {
			/* Switch the power off for the port */
			power_trans |= PORT_BIT(i) << 4;
		} else if (cfg->port_mode[i] == PORT_POWER_MODE_OFF) {
			/*
			 * Switch the port off completely.
			 */
			res = tps23861_read_byte(file, PT_MODE_REG);
			if (res < 0)
				continue;
			tps23861_write_byte(file, PT_MODE_REG,
			    res & ~(3 << (i << 1)));
		}
	}

	/* Commit calculated power state if there's something to do */
	if (power_trans != 0) {
		/* Switch the power off for the port */
		res = tps23861_write_byte(file, PT_POWER_EN_REG, power_trans);
		if (res < 0)
			syslog(LOG_ERR,
			    "Failed to apply power transition 0x%x\n",
			    power_trans);

		/* Wait for command to take effect */
		usleep(3000);
	}
}

static void
tps23861_update_config(int file, const char *cfgfiles[], int num_configs,
    struct tps23861_config *cfg)
{
	struct json_object *top_obj, *port_obj, *mode_obj;
	char port_name[sizeof("PortN")];
	const char *mode, *filename;
	int i, opmode;

	/* Look for first file that is available */
	filename = NULL;
	for (i = 0; i < num_configs; i++) {
		if (access(cfgfiles[i], F_OK) == 0) {
			filename = cfgfiles[i];
			break;
		}
	}
	if (filename == NULL)
		return;

        top_obj = json_object_from_file(filename);
	if (top_obj == NULL) {
		syslog(LOG_ERR, "Error: could not read config file %s\n",
		    filename);
		return;
	}

	for (i = 1; i < 4; i++) {
		snprintf(port_name, sizeof(port_name), "Port%d", i);
		if (!json_object_object_get_ex(top_obj, port_name, &port_obj))
			continue;

		if (!json_object_object_get_ex(port_obj, "mode", &mode_obj) ||
		    !json_object_is_type(mode_obj, json_type_string))
			continue;

		mode = json_object_get_string(mode_obj);

		if (strcasecmp(mode, "on") == 0)
			cfg->port_mode[i] = PORT_POWER_MODE_ON;
		else if (strcasecmp(mode, "off") == 0)
			cfg->port_mode[i] = PORT_POWER_MODE_OFF;
		else if (strcasecmp(mode, "auto") == 0)
			cfg->port_mode[i] = PORT_POWER_MODE_AUTO;
		else {
			syslog(LOG_ERR, "Port %d mode '%s' not recognized\n",
			    i, mode);
			continue;
		}

		syslog(LOG_NOTICE, "Monitoring port %d in mode '%s'\n",
			    i, mode);
	}

	json_object_put(top_obj);

	/*
	 * Always enable detection and classification on port 0 and leave it in AUTO mode,
	 * this daemon does not control it.
	 */
	cfg->port_autodisc = PORT_BIT(0) | (PORT_BIT(0) << 4);
	opmode = 0x03 << 0;
	/*
	 * Configure pseudo-PoE ports. There is no chance classification will ever work
	 * with these ports, do not bother enabling it, deal only with detection bits.
	 */
	for (i = 1; i < 4; i++) {
		/* Disable detection for ports forced on */
		if (cfg->port_mode[i] != PORT_POWER_MODE_ON)
			cfg->port_autodisc |= PORT_BIT(i);
		/* Do not bother with ports in permanent OFF mode */
		if (cfg->port_mode[i] != PORT_POWER_MODE_OFF)
			opmode |= (0x01 << (i << 1));
	}
	/* If port has detection enabled, enable disconnect as well */
	tps23861_write_byte(file, PT_DISCON_EN_REG, cfg->port_autodisc & 0x0F);
	/* Set ports to operate in respective selected mode */
	tps23861_write_byte(file, PT_MODE_REG, opmode);
	/*
	 * Specification calls for 1.2ms delay  after this register
	 * is written before Detect/Class Enable (0x14) write command.
	 * Be on the save side and sleep always.
	 */
	usleep(3000);
}

static struct option options[] = {
	{ "config",	required_argument, 0, 'c' },
	{ "daemon",	no_argument, 0, 'D' },
	{ "force",	no_argument, 0, 'f' },
	{ "help",	no_argument, 0, 'h' },
	{ NULL,		0,           0,  0  },
};

static sig_atomic_t config_reload;

static void
tps23861_signal(int signal)
{
	config_reload = 1;
}

static void
tps23861_setup_signals(void)
{
	struct sigaction sa, osa;

	memset(&sa, 0, sizeof(sa));

	sa.sa_handler = tps23861_signal;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);

	sigaction(SIGHUP, &sa, &osa);
}

static const char *progname;

static void
usage(int eval, FILE *outf)
{
	fprintf(outf, "Usage: %s [--daemon|-D]\n",
	    progname);
	exit(eval);
}

#define MAX_CONFIG_FILES 2

int main(int argc, char *argv[])
{
	struct tps23861_config cfg;
	const char *config_files[MAX_CONFIG_FILES];
	int i, res, address, file, index, reload, num_configs;
	int daemonize = 0;
	int force = 0;

	address = TPS23861_I2C_ADDR;
	memset(config_files, 0, sizeof(MAX_CONFIG_FILES));
	num_configs = 0;

	/* Figure our own short name */
	progname = strdup(basename(argv[0]));

	while ((res = getopt_long(argc, argv, "c:Dfh",
		    options, &index)) != -1) {
		switch (res) {
		case 'c':
			if (num_configs >= MAX_CONFIG_FILES)
				usage(1, stderr);
			config_files[num_configs++] = optarg;
			break;
		case 'D':
			daemonize = 1;
			break;
		case 'f':
			force = 1;
			break;
		case 'h':
			usage(0, stdout);
			break;
		default:
			usage(1, stderr);
			break;
		}
	}
	if (argc != optind)
		usage(1, stderr);

	/* Attempt to log to console and stderr both */
	openlog("tps23861", LOG_CONS | LOG_PERROR, LOG_DAEMON);

	/* Become a daemon, if requested to do so */
	if (daemonize != 0) {
		if (daemon(0, 0) == -1) {
			syslog(LOG_ERR,
				"Error: could not daemonize: %s\n",
				strerror(errno));
			return 1;
		}
	}

	/* Try to find the controller */
	file = tps23861_probe(address);
	if (file < 0)
		return 1;

	/* Initialize the controller */
	res = tps23861_setup(file);
	if (res < 0)
		return 1;

	/*
	 * Initialize default configuration:
	 *    auto manage all ports
	 *    ports connection status is not known
	 */
	memset(&cfg, 0, sizeof(cfg));
	cfg.port_autodisc = (PORT_BIT(0) << 4);
	for (i = 0; i < 4; i++) {
		cfg.port_mode[i]  = PORT_POWER_MODE_AUTO;
		cfg.port_state[i] = PORT_STATE_UNKNOWN;
		cfg.port_power[i] = -1;
		cfg.port_autodisc |= PORT_BIT(i);
	}

	/* Force reload of config file on first poll iteration */
	if (num_configs != 0) {
		config_reload = 1;

		/* Re-read config file on SIGHUP */
		tps23861_setup_signals();
	}

	/* Loop forever */
	while (1) {
		/* Re-read configuration file, if necessary */
		if (config_reload) {
			config_reload = 0;
			tps23861_update_config(file, config_files, num_configs,
			    &cfg);
		}

		/* Check for changes, apply config file settings */
		tps23861_poll(file, &cfg);

		/* Wait for next try */
		sleep(1);
	}

	return 0;
}
