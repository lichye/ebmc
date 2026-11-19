#ifndef CPROVER_EBMC_OUTPUT_BTOR2_H
#define CPROVER_EBMC_OUTPUT_BTOR2_H

#include <string>

class ebmc_propertiest;
class transition_systemt;

/// Serialize a word-level safety problem. Throws ebmc_errort for constructs
/// that cannot be represented, before the caller opens the output file.
std::string output_btor2(const transition_systemt &, const ebmc_propertiest &);

#endif // CPROVER_EBMC_OUTPUT_BTOR2_H
