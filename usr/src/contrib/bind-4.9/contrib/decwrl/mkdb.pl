#! /usr/bin/perl
#
# $Header: mkdb,v 1.36 93/01/14 01:02:41 root Exp $
#
# from a hosts(5) database on standard input or argv,
# build the .data and .zone files needed by named.boot

#++ BEGIN user servicable parts
$Domain = 'pa.dec.com';
$Zone = 'decpa';
@Gateway_MX = (
	'10 pobox1',
	'20 inet-gw-1',
	'20 inet-gw-2'
);
#-- END user servicable parts (more later, though)

umask(022);
chop($date = `/bin/date`);
$Serial_File = 'version';
($Domain_Pat = "$Domain") =~ s/(\W)/\\$1/go;
$Domain_Pat = "\\.$Domain_Pat\$";

&Update_Serial_Number();
&Update_Zone_Serials();	
&Init_Zone_Datafiles();
&Churn();
&Close_Zone_Datafiles();

exit 0;

sub Write_RR {
	local($fh, $name, $type, $data) = @_;

	if ($name eq $Write_RR'last_name{$fh}) {
		$name = '';
	} else {
		$Write_RR'last_name{$fh} = $name;
	}
	printf $fh "%-30s in %-5s %s\n", $name, $type, $data;
}

sub Update_Serial_Number {
	local(*f);

	if (open(f, "<$Serial_File") && ($Serial_No = <f>)) {
		close(f);
	} else {
		$Serial_No = 0;
	}

	$Serial_No++;

	open(f, ">$Serial_File") || die "$Serial_File: $!";
	print f "$Serial_No\n";
	close(f);

	print "*** New serial number is $Serial_No\n";
}

sub Update_Zone_Serials {
	local(*f, $zonefile, $zone);

	foreach $zonefile (<*.zone>) {
		$zone = $zonefile;
		$zone =~ s/\.zone$//;
		$Zones{$zone}++ unless ($zone eq $Zone);
		print "*** updating zone file '$zonefile'\n";
		open(f, "<$zonefile") || die "$zonefile: $!";
		@f = <f>;
		open(f, ">$zonefile.NEW") || die "$zonefile.NEW: $!";
		foreach (@f) {
			$_ = $`.$Serial_No.$1."\n" if (/\d+(\s+\;Serial)/);
			print f;
		}
		close(f) || die "$zonefile.NEW: $!";
	}

	print "*** Zone file serial numbers updated\n";
}

sub Init_Zone_Datafiles {
	#
	# our name servers are considered authoritative for the following
	# zones of in-addr.arpa.  we create a .data file for each here.  None
	# of them have SOA's since we expect them to be $INCLUDE'd by .zone
	# files.
	#
	local($zone);

	foreach $zone (sort(keys(%Zones))) {
		print "*** Init'ing zone ".$zone."\n";
		open($zone, ">$zone.data.NEW") || die "$zone.data.NEW: $!";
		$rzone = join('.', reverse(split(/\./, $zone)))
		       . '.in-addr.arpa.';
		print $zone "; $zone.data ($zone - $rzone) $date\n";
		print $zone '$ORIGIN '.$rzone."\n";
	}

	# the main host->addr file has no origin statement in it since
	# its name is not mechanically translatable to its zone.
	open($Zone, ">$Zone.data.NEW") || die "$Zone.data.NEW: $!";
	foreach (@Gateway_MX) {
		&Write_RR($Zone, '@', 'mx', $_);
	}
}

sub Close_Zone_Datafiles {
	foreach ($Zone, keys(%Zones)) {
		close($_) || die "$_.data.NEW: $!";
		rename("$_.data", "$_.data.BAK");
		rename("$_.data.NEW", "$_.data") || die "rename1 ($_): $!";
		rename("$_.zone", "$_.zone.BAK");
		rename("$_.zone.NEW", "$_.zone") || die "rename2 ($_): $!";
	}
}

sub Churn {
    print "*** Churning\n";
host:
    while (<>) {
	chop;

	# capture and convert comments
	if (/^\s*\#/) {
		$_ = ';' . $';
		print $Zone "$_\n";
		last host if (/BELOW HERE NOT REGISTERED WITH DOMAIN SERVER/);
		next host;
	}
	s/\s*\#.*$//;

	# parse line, skip it if addr doesn't have four components
	y/A-Z/a-z/;
	($a, $name, @aliases) = split;
	next host if (4 != (@a = split(/\./, $a)));
	$name =~ s/$Domain_Pat//o;

	#
	# names that still have dots need to be qualified since they
	# aren't local.
	#
	# only local names get MX RR's generated for them.
	#
	if ($name =~ /\./) {
		$fullname = $name . '.';
	} else {
		&Write_RR($Zone, $name, 'a', $a);
		#
		# do not generate mx rr's for network or broadcast a rr's
		# or for loopback addresses
		#
		if ($a[0] != 127 && $a[3] != 0 && $a[3] != 255) {
			&Write_RR($Zone, $name, 'mx', "0 $name");
			foreach (@Gateway_MX) {
				&Write_RR($Zone, $name, 'mx', $_);
			}
		}
		$fullname = $name . ".$Domain.";
	}

	#++ BEGIN user servicable parts
	if ($a[0] == 16) {
		$zone = '16.' . $a[1];
		$ptr = $a[3] . '.' . $a[2];
	} else {
		$zone = 'bogus';
		$ptr = 'bogus';
	}
	#-- END user servicable parts

	#
	# if this is a zone we support, write out a PTR for it
	#
	if ($Zones{$zone}) {
		&Write_RR($zone, $ptr, 'ptr', $fullname);
	}

	#
	# iterate over the aliases, if any, and generate CNAMEs and PTRs.
	#
	foreach (@aliases) {
		s/$Domain_Pat//o;

		# aliases that are the same as the basename aren't useful
		next if ($_ eq $name);

		# CNAME makes sense only for local names
		if (! /\./) {
			$maybedot = '.' if ($name =~ /\./);
			&Write_RR($Zone, $_, 'cname', $name.$maybedot);
		}

		# PTR makes sense only for zones we support (NOTE: $_ smashed)
		if ($Zones{$zone}) {
			$_ .= ".$Domain" unless /\./;
			&Write_RR($zone, $ptr, 'ptr', "$_.");
		}
	}
    }
}
