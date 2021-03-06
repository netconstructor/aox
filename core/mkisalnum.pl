#!/usr/bin/perl

open( I, "< /usr/share/perl/5.10.0/unicore/UnicodeData.txt" ) || die;
while ( <I> ) {
    #0069;LATIN SMALL LETTER I;Ll;0;L;;;;;N;;;0049;;0049
    ($cp,$desc,$cl,$j,$j,$j,$j,$j,$j,$j,$j,$j,$equiv) = split( /;/, $_ );
    $isalpha[hex $cp] = 1 if ( $cl =~ /^L/ );
    $isdigit[hex $cp] = 1 if ( $cl =~ /^N/ );
    $desc[hex $cp] = $desc;
}

$n = $#isalpha;
$n = $#isdigit if ( $#isdigit > $n );

open( O, "> unicode-isalnum.inc" ) || die;
$i = 0;
print O '// Generated by $Id$', "\n",
        "static const uint numLetters = ", $#isalpha + 1, ";\n",
        "static const uint numDigits = ", $#isdigit + 1, ";\n",
        "static const struct { bool isAlpha:1; bool isDigit:1; } unidata[", $n + 1, "] = {\n";
while ( $i <= $n ) {
    print O "    { ";
    if ( defined( $isalpha[$i] ) ) {
        print O "true, ";
    }
    else {
        print O "false, ";
    }
    if ( defined( $isdigit[$i] ) ) {
        print O "true";
    }
    else {
        print O "false";
    }
    print O " }";
    print O "," if ( $i < $n );
    print O " // ", $desc[$i], "\n";
    $i++;
}
print O "};\n";
