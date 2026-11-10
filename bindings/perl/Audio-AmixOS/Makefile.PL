# Audio::AmixOS: Perl XS binding over the A mix OS C SDK (sdk/c/include/amix.h, libamix).
#   AMIX_INC=/path/to/sdk/c/include AMIX_LIB=/path/to/lib perl Makefile.PL && make && make test
use strict; use warnings; use ExtUtils::MakeMaker; use File::Spec;
my $root = File::Spec->rel2abs(File::Spec->catdir('..', '..', '..'));
my $inc = $ENV{AMIX_INC} // File::Spec->catdir($root, 'sdk', 'c', 'include');
my $lib = $ENV{AMIX_LIB} // File::Spec->catdir($root, 'build');
WriteMakefile(
  NAME => 'Audio::AmixOS', VERSION_FROM => 'lib/Audio/AmixOS.pm', ABSTRACT => 'A mix OS audio engine (mixer, meters, devices) from Perl',
  AUTHOR => 'AMFB Audio', LICENSE => 'open_source',
  INC => "-I$inc", LIBS => ["-L$lib -lamix"], TYPEMAPS => ['typemap'],
  dynamic_lib => { OTHERLDFLAGS => "-Wl,-rpath,$lib" },
  MIN_PERL_VERSION => '5.016',
);
