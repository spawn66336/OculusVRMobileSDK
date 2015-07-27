#!/usr/bin/perl

use File::Basename;
 
# The name of the file
$inFile  = $ARGV[0];
$outFile = $ARGV[1];
 
# The name of the image is the body of the input path
my ($name)      = $inFile =~ m!(\w+)\.\w+$!;
my ($extension) = $inFile =~ /\.([^.]+)$/;
#($name,$hlrciufglhclnbklldichbjcnlrfujin,$extension) = fileparse("$inFile",qr"\..[^.]*$");
 
print "Converting image $name $extension from $inFile to $outFile\n";
 
# Slurp all the input
undef $/;
open(FILE, "$inFile") || die "unable to open $inFile\n";
$in = <>;
close FILE;
 
# Open output file
open(FILE, ">$outFile") || die "unable to open $outFile for writing\n";
 
# Create an array of unsigned chars from input
@chars = unpack "C*", $in;
 
# Output
print FILE "// Automatically generated by embendimg. Not modify.\n";
print FILE "#ifndef __EMBEDIMG_${name}\n";
print FILE "#define __EMBEDIMG_${name}\n\n";
print FILE "const unsigned char g_${name}_${extension}[] = \n{\n  ";
 
foreach $char (@chars)
{
    printf FILE "0x%02x", $char;
    last if $i == $#chars;
    print FILE ((++$i % 13) ? ", " : ",\n  ");
}
 
print FILE "\n};\n\n#endif\n";
close FILE;