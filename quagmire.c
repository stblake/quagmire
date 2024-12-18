//
//	Polyalphabetic cipher solver
//

// A stochastic, shotgun-restarted hill climber with backtracking for solving 
// Vigenere, Beaufort, and Quagmire I - IV with variants. 

// Written by Sam Blake, started 14 July 2023. 

/*
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/

// TODO: 	- remove decryption from state_score. 

// Reference for n-gram data: http://practicalcryptography.com/cryptanalysis/letter-frequencies-various-languages/english-letter-frequencies/

#include "quagmire.h"

/* Program syntax:

	$ ./quagmire \
		-nhillclimbs /number of hillclimbing steps/ \
		-nrestarts /number of restarts/ \
		-type /cipher type (0, 1, 2, 3, 4, or 5)/ \
		-cipher /ciphertext file/ \
		-crib /crib file/ \
		-ngramsize /n-gram size in n-gram statistics file/ \
		-ngramfile /n-gram statistics file/ \
		-maxkeywordlen /max length of the keyword/ \
		-maxcyclewordlen /max length of the cycleword/ \
		-plaintextkeywordlen /user defined length of the plaintext keyword/ \
		-ciphertextkeywordlen /user defined length of the ciphertext keyword/ \
		-cyclewordlen /user defined length of the cycleword/ \
		-nsigmathreshold /n sigma threshold for candidate keyword length/ \
		-backtrackprob /probability of backtracking to the best 
			solution instead of a random initial solution/ \
		-keywordpermprob /probability of permuting the keyword instead of the cycleword/ \
		-slipprob /probability of slipping to a worse score/ \
		-iocthreshold /lower limit for ioc/ \
		-dictionary /dictionary file, a text file containing one word per line/ \
		-weightngram /weight used in the hillclimber score for the ngram score/ \
		-weightcrib /weight used in the hillclimber score for the crib matches/ \
		-weightioc /weight used in the hillclimber score for the IoC/ \
		-weightentropy /weight used in the hillclimber score for the plaintext entropy/ \
		-verbose


	Notes: 

		/quagmire cipher type (0,1,2,3, or 4)/ -- type 0 is a Vigenere cipher, then 1-4 are Quagmire types
			1 to 4 as defined by the ACA (https://www.cryptogram.org/resource-area/cipher-types/), type 5 
			is the Beaufort cipher as defined by the ACA (https://www.cryptogram.org/downloads/aca.info/ciphers/Beaufort.pdf)

		/ciphertext file/ -- the entire cipher should be on the first line of the file. Subsequent 
			lines will not be read. 

		/crib file/ -- uses "_" for unknown chars. Just a single line of the same length
			as the ciphers contained in the cipher file. For the Kryptos K4 cipher (assuming Sanborn has not 
			made any enciphering and/or spelling and/or positional mistakes) it should contain

		_____________________EASTNORTHEAST_____________________________BERLINCLOCK_______________________
	

	Performance: 

		This program is designed for attacks on the final unsolved Kryptos cipher (K4), which is only of 
		length 97. For longer ciphers a far better approach is to use frequency analysis on each simple 
		substitution ciphers (once the period has been estimated). Furthermore, if we knew for certain that 
		the cribs given for K4 were correct we could make additional performance improvements. 
*/

int main(int argc, char **argv) {

	int i, j, k, cipher_type = 3, cipher_len, cycleword_len, ngram_size = 0,
		ciphertext_keyword_len = 5, plaintext_keyword_len = 5, ciphertext_max_keyword_len = 12, 
		min_keyword_len = 5, plaintext_max_keyword_len = 12, max_cycleword_len = 20, n_restarts = 1, 
		n_cycleword_lengths, n_hill_climbs = 1000, n_cribs, best_cycleword_length,
		best_plaintext_keyword_length, best_ciphertext_keyword_length, n_words_found, 
		cipher_indices[MAX_CIPHER_LENGTH], crib_positions[MAX_CIPHER_LENGTH], 
		crib_indices[MAX_CIPHER_LENGTH], cycleword_lengths[MAX_CIPHER_LENGTH],
		decrypted[MAX_CIPHER_LENGTH], best_decrypted[MAX_CIPHER_LENGTH],
		plaintext_keyword[ALPHABET_SIZE], ciphertext_keyword[ALPHABET_SIZE], cycleword[ALPHABET_SIZE],
		best_plaintext_keyword[ALPHABET_SIZE], best_ciphertext_keyword[ALPHABET_SIZE], best_cycleword[ALPHABET_SIZE]; 
	double n_sigma_threshold = 1., ioc_threshold = 0.047, backtracking_probability = 0.01, 
		keyword_permutation_probability = 0.01, slip_probability = 0.0005, score, best_score;
	float weight_ngram = 12., weight_crib = 36., weight_ioc = 1., weight_entropy = 1.;
	char ciphertext_file[MAX_FILENAME_LEN], crib_file[MAX_FILENAME_LEN], dictionary_file[MAX_FILENAME_LEN], 
		ngram_file[MAX_FILENAME_LEN], ciphertext[MAX_CIPHER_LENGTH], 
		cribtext[MAX_CIPHER_LENGTH];
	bool verbose = false, cipher_present = false, crib_present = false, plaintext_keyword_len_present = false, 
		cycleword_len_present = false, ciphertext_keyword_len_present = false, dictionary_present_p = false,
		variant = false, beaufort = false;
	FILE *fp;
	float *ngram_data;

	// Read command line args. 
	for(i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-type") == 0) {
			cipher_type = atoi(argv[++i]);
			printf("\n-type %d", cipher_type);
		} else if (strcmp(argv[i], "-cipher") == 0) {
			cipher_present = true;
			strcpy(ciphertext_file, argv[++i]);
			printf("\n-cipher %s", ciphertext_file);
		} else if (strcmp(argv[i], "-crib") == 0) {
			crib_present = true;
			strcpy(crib_file, argv[++i]);
			printf("\n-crib %s", crib_file);
		} else if (strcmp(argv[i], "-ngramsize") == 0) {
			ngram_size = atoi(argv[++i]);
			printf("\n-ngram_size %d", ngram_size);
		} else if (strcmp(argv[i], "-ngramfile") == 0) {
			strcpy(ngram_file, argv[++i]);
			printf("\n-ngramfile %s", ngram_file);
		} else if (strcmp(argv[i], "-maxkeywordlen") == 0) {
			plaintext_keyword_len = atoi(argv[++i]);
			ciphertext_keyword_len = plaintext_keyword_len;
			printf("\n-maxkeywordlen %d", plaintext_keyword_len);
		} else if (strcmp(argv[i], "-keywordlen") == 0) {
			plaintext_keyword_len_present = true;
			ciphertext_keyword_len_present = true;
			plaintext_keyword_len = atoi(argv[++i]);
			ciphertext_keyword_len = plaintext_keyword_len;
			plaintext_max_keyword_len = max(plaintext_max_keyword_len, 1 + plaintext_keyword_len);
			ciphertext_max_keyword_len = max(ciphertext_max_keyword_len, 1 + ciphertext_keyword_len);
			min_keyword_len = plaintext_keyword_len;
			printf("\n-keywordlen %d", plaintext_keyword_len);			
		} else if (strcmp(argv[i], "-plaintextkeywordlen") == 0) {
			plaintext_keyword_len_present = true;
			plaintext_keyword_len = atoi(argv[++i]);
			plaintext_max_keyword_len = max(plaintext_max_keyword_len, 1 + plaintext_keyword_len);
			min_keyword_len = plaintext_keyword_len;
			printf("\n-plaintextkeywordlen %d", plaintext_keyword_len);
		} else if (strcmp(argv[i], "-ciphertextkeywordlen") == 0) {
			ciphertext_keyword_len_present = true;
			ciphertext_keyword_len = atoi(argv[++i]);
			ciphertext_max_keyword_len = max(ciphertext_max_keyword_len, 1 + ciphertext_keyword_len);
			min_keyword_len = ciphertext_keyword_len;
			printf("\n-ciphertextkeywordlen %d", ciphertext_keyword_len);
		} else if (strcmp(argv[i], "-maxcyclewordlen") == 0) {
			max_cycleword_len = atoi(argv[++i]);
			printf("\n-maxcyclewordlen %d", max_cycleword_len);
		} else if (strcmp(argv[i], "-cyclewordlen") == 0) {
			cycleword_len_present = true;
			cycleword_len = atoi(argv[++i]);
			if (cycleword_len == 0) {
				cycleword_len_present = false;
			}
			max_cycleword_len = max(max_cycleword_len, 1 + cycleword_len);
			printf("\n-cyclewordlen %d", cycleword_len);
		} else if (strcmp(argv[i], "-nsigmathreshold") == 0) {
			n_sigma_threshold = atof(argv[++i]);
			printf("\n-nsigmathreshold %.2f", n_sigma_threshold);
		} else if (strcmp(argv[i], "-nlocal") == 0) {
			// TODO: remove me and update all tests/scripts etc. 
		} else if (strcmp(argv[i], "-nhillclimbs") == 0) {
			n_hill_climbs = atoi(argv[++i]);
			printf("\n-nhillclimbs %d", n_hill_climbs);			
		} else if (strcmp(argv[i], "-nrestarts") == 0) {
			n_restarts = atoi(argv[++i]);
			printf("\n-nrestarts %d", n_restarts);
		} else if (strcmp(argv[i], "-backtrackprob") == 0) {
			backtracking_probability = atof(argv[++i]);
			printf("\n-backtrackprob %.4f", backtracking_probability);
		} else if (strcmp(argv[i], "-keywordpermprob") == 0) {
			backtracking_probability = atof(argv[++i]);
			printf("\n-keywordpermprob %.4f", keyword_permutation_probability);
		} else if (strcmp(argv[i], "-slipprob") == 0) {
			slip_probability = atof(argv[++i]);
			printf("\n-slipprob %.4f", slip_probability);
		} else if (strcmp(argv[i], "-iocthreshold") == 0) {
			ioc_threshold = atof(argv[++i]);
			printf("\n-iocthreshold %.4f", ioc_threshold);
		} else if (strcmp(argv[i], "-dictionary") == 0 || strcmp(argv[i], "-dict") == 0) {
			dictionary_present_p = true;
			strcpy(dictionary_file, argv[++i]);
			printf("\n-dictionary %s", dictionary_file);
		} else if (strcmp(argv[i], "-weightngram") == 0) { 
			weight_ngram = atof(argv[++i]);
			printf("\n-weightngram %.4f", weight_ngram);
		} else if (strcmp(argv[i], "-weightcrib") == 0) { 
			weight_crib = atof(argv[++i]);
			printf("\n-weightcrib %.4f", weight_crib);
		} else if (strcmp(argv[i], "-weightioc") == 0) { 
			weight_ioc = atof(argv[++i]);
			printf("\n-weightioc %.4f", weight_ioc);
		} else if (strcmp(argv[i], "-weightentropy") == 0) { 
			weight_entropy = atof(argv[++i]);
			printf("\n-weightentropy %.4f", weight_entropy);
		} else if (strcmp(argv[i], "-variant") == 0) { 
			variant = true;
			printf("\n-variant");
		} else if (strcmp(argv[i], "-verbose") == 0) {
			verbose = true;
			printf("\n-verbose ");
		} else {
			printf("\n\nERROR: unknown arg '%s'\n\n", argv[i]);
			return 0;
		}
	}
	printf("\n\n");


	if (cipher_type == BEAUFORT) {
		beaufort = true;
	}

	// Print cipher type. 

	char variant_display[10], variant_str[] = "variant";
	if (variant) {
		strcpy(variant_display, variant_str);
	} else {
		strcpy(variant_display, "");
	}

	if (cipher_type == VIGENERE) {
		printf("\n\nSolving a %s Vigenere cipher.\n\n", variant_display);
	} else if (cipher_type == BEAUFORT) {
		printf("\n\nSolving a %s Beaufort cipher.\n\n", variant_display);
	} else if (cipher_type == QUAGMIRE_1) {
		printf("\n\nSolving a %s Quagmire I cipher.\n\n", variant_display);
	} else if (cipher_type == QUAGMIRE_2) {
		printf("\n\nSolving a %s Quagmire II cipher.\n\n", variant_display);
	} else if (cipher_type == QUAGMIRE_3) {
		printf("\n\nSolving a %s Quagmire III cipher.\n\n", variant_display);
	} else if (cipher_type == QUAGMIRE_4) {
		printf("\n\nSolving a %s Quagmire IV cipher.\n\n", variant_display);
	}


	// Sense check command line inputs. 

	if (! cipher_present) {
		printf("\n\nERROR: cipher file not present.\n\n");
		return 0;
	}

	if (ngram_size == 0) {
		printf("\n\nERROR: -ngramsize missing.\n\n");
		return 0;
	}

	if (! file_exists(ciphertext_file)) {
		printf("\nERROR: missing file '%s'\n", ciphertext_file);
  		return 0;
	}

	if (! file_exists(ngram_file)) {
		printf("\nERROR: missing file '%s'\n", ngram_file);
  		return 0;
	}

	if (crib_present && ! file_exists(crib_file)) {
		printf("\nERROR: missing file '%s'\n", crib_file);
  		return 0;
	}

	// Check if OxfordEnglishWords.txt is present. 

	char oxford_english_words[] = "OxfordEnglishWords.txt";
	if (! dictionary_present_p && file_exists(oxford_english_words)) {
		dictionary_present_p = true;
		strcpy(dictionary_file, oxford_english_words);
		if (verbose) {
			printf("\ndictionary = %s\n\n", dictionary_file);
		}
	}

	// Read ciphertext. Only the first line of the ciphertext file is read (leaving 
	// further lines for explanation/derivation etc.)

	fp = fopen(ciphertext_file, "r");
	fscanf(fp, "%s", ciphertext);
	fclose(fp);

	if (verbose) {
		printf("ciphertext = \n\'%s\'\n\n", ciphertext);
	}

	cipher_len = (int) strlen(ciphertext);

	// Read crib. 

	if (file_exists(crib_file)) {

		fp = fopen(crib_file, "r");
		fscanf(fp, "%s", cribtext);
		fclose(fp);

		if (verbose) {
			printf("cribtext = \n\'%s\'\n\n", cribtext);
		}

		// Check ciphertext and cribtext are of the same length. 

		if (cipher_len != strlen(cribtext)) {
			printf("\n\nERROR: strlen(ciphertext) = %d, strlen(cribtext) = %lu.\n\n", 
				cipher_len, strlen(cribtext));
			return 0; 
		}

		// Extract crib positions and corresponding plaintext. 

		if (verbose) {
			printf("\ncrib indices = \n\n");
		}

		n_cribs = 0;
		for (i = 0; i < cipher_len; i++) {
			if (cribtext[i] != '_') {
				crib_positions[n_cribs] = i;
				crib_indices[n_cribs] = cribtext[i] - 'A';
				n_cribs++;
				if (verbose) {
					printf("%d, %c, %d\n", i, cribtext[i], cribtext[i] - 'A');
				}
			}
		}

		if (verbose) {
			printf("\n");
		}
	} else {
		// No cribs present. 
		n_cribs = 0;
	}

	// Compute ciphertext indices. A -> 0, B -> 1, ..., Z -> 25 (Assuming ALPHABET_SIZE = 26)

	ord(ciphertext, cipher_indices);

	// Estimate cycleword length. 

	estimate_cycleword_lengths(
			cipher_indices, 
			cipher_len, 
			max_cycleword_len, 
			n_sigma_threshold,
			ioc_threshold, 
			&n_cycleword_lengths, 
			cycleword_lengths, 
			verbose);

	// Load n-gram file. 

	ngram_data = load_ngrams(ngram_file, ngram_size, verbose);

	// Set random seed.

	srand(time(NULL));

	// User-defined cycleword length. 
	
	if (cycleword_len_present) {
		n_cycleword_lengths = 1;
		cycleword_lengths[0] = cycleword_len;
	}

	// Vigenere cipher case.
	if (cipher_type == VIGENERE) {
		min_keyword_len = 1;
	}

	// Beaufort cipher case.
	if (cipher_type == BEAUFORT) {
		min_keyword_len = 1;
		plaintext_max_keyword_len = 2;
		plaintext_max_keyword_len = 2;
	}

	// For each cycleword length and keyword length combination, run the 'shotgun' hill-climber. 

	best_score = 0.;

	for (i = 0; i < n_cycleword_lengths; i++) {
		for (j = min(min_keyword_len, plaintext_keyword_len); j < plaintext_max_keyword_len; j++) {
			for (k = min(min_keyword_len, ciphertext_keyword_len); k < ciphertext_max_keyword_len; k++) {
				
				// printf("i,j,k = %d, %d, %d", i, j, k);

				// User-specified plaintext keyword length. 
				if (plaintext_keyword_len_present && j != plaintext_keyword_len) {
					continue ;
				}

				// User-specified ciphertext keyword length. 
				if (ciphertext_keyword_len_present && k != ciphertext_keyword_len) {
					continue ;
				}

				// Both Vigenere and Quagmire 3 use the same ciphertext and plaintext keywords. 
				if ((cipher_type == VIGENERE || cipher_type == QUAGMIRE_3) && j != k) continue ;

				// Vigenere cipher uses same ciphertext, plaintext, and cycleword lengths.
				if (cipher_type == VIGENERE && ! (cycleword_lengths[i] == j && cycleword_lengths[i] == k)) continue ;

				// Beaufort cipher uses a plaintext and ciphertext keyword of 'A'.
				if (cipher_type == BEAUFORT && ! (j == 1 && k == 1)) continue ;

				if (verbose) {
					printf("\nplaintext, ciphertext, cycleword lengths = %d, %d, %d\n", j, k, cycleword_lengths[i]);
				}

				// Check the cipher satisfies the cribs for the cycleword length. 
				if (! cribs_satisfied_p(cipher_indices, cipher_len, crib_indices, crib_positions, n_cribs, cycleword_lengths[i], verbose)) {
					if (verbose) {
						printf("\n\nCiphertext does not satisfy the cribs for cycleword length %d. \n\n", cycleword_lengths[i]);
					}
#if CRIB_CHECK
					continue ;
#endif
				}

				// Run the hill-climber. 

				score = quagmire_shotgun_hill_climber(
					cipher_type, 
					cipher_indices, 
					cipher_len, 
					crib_indices, 
					crib_positions, 
					n_cribs, 
					cycleword_lengths[i],
					j,  
					k,
					n_hill_climbs, 
					n_restarts, 
					ngram_data, 
					ngram_size,
					decrypted, 
					plaintext_keyword,
					ciphertext_keyword,
					cycleword, 
					backtracking_probability,
					keyword_permutation_probability,
					slip_probability, 
					weight_ngram, 
					weight_crib, 
					weight_ioc, 
					weight_entropy,
					variant, 
					beaufort,
					verbose);

				// Keep the best solution. 

				if (score > best_score) {
					best_score = score;
					best_cycleword_length = cycleword_lengths[i];
					best_plaintext_keyword_length = j;
					best_ciphertext_keyword_length = k;
					vec_copy(decrypted, best_decrypted, cipher_len);
					vec_copy(plaintext_keyword, best_plaintext_keyword, ALPHABET_SIZE);
					vec_copy(ciphertext_keyword, best_ciphertext_keyword, ALPHABET_SIZE);
					vec_copy(cycleword, best_cycleword, ALPHABET_SIZE);
				}
			}
		}
	}

	// Find dictionary words. 

	char plaintext_string[MAX_CIPHER_LENGTH];

	for (int i = 0; i < cipher_len; i++) {
		plaintext_string[i] = best_decrypted[i] + 'A';
	}
	plaintext_string[cipher_len] = '\0';

#if DICTIONARY
	if (dictionary_present_p) {
		char **dict = NULL;
		int n_dict_words, max_dict_word_len;

		load_dictionary(dictionary_file, &dict, &n_dict_words, &max_dict_word_len, verbose);

		if (verbose) {
			printf("\nDictionary words = \n");
		}

		n_words_found = find_dictionary_words(plaintext_string, dict, n_dict_words, max_dict_word_len);
		printf("\n%d words found.\n", n_words_found);

		free_dictionary(dict, n_dict_words);
	}
#endif

	printf("\n\n%.2f\n", best_score);
	if (dictionary_present_p) {
		printf("%d\n", n_words_found);
	}

	print_text(cipher_indices, cipher_len);
	printf("\n");
	print_text(best_plaintext_keyword, ALPHABET_SIZE);
	printf("\n");
	print_text(best_ciphertext_keyword, ALPHABET_SIZE);
	printf("\n");
	print_text(best_cycleword, best_cycleword_length);
	printf("\n");
	print_text(best_decrypted, cipher_len);
	printf("\n\n");

	// K4-specific checks for BERLIN, CLOCK, EAST, NORTH, BERLINCLOCK and EASTNORTHEAST. 

#if KRYPTOS
	bool berlin_present = false, clock_present = false, east_present = false, north_present = false, 
		berlinclock_present = false, eastnortheast_present = false; 

	if (strstr(plaintext_string, "BERLIN") != NULL) {
		berlin_present = true;
		printf("**** \'BERLIN\' PRESENT!!! ****\n");
	}

	if (strstr(plaintext_string, "CLOCK") != NULL) {
		clock_present = true;
		printf("**** \'CLOCK\' PRESENT!!! ****\n");
	}

	if (strstr(plaintext_string, "EAST") != NULL) {
		east_present = true;
		printf("**** \'EAST\' PRESENT!!! ****\n");
	}

	if (strstr(plaintext_string, "NORTH") != NULL) {
		north_present = true;
		printf("**** \'NORTH\' PRESENT!!! ****\n");
	}

	if (strstr(plaintext_string, "BERLINCLOCK") != NULL) {
		berlinclock_present = true;
		for (i = 0; i < 1000; i++) {
			printf("**** \'BERLINCLOCK\' PRESENT!!! ****");
		}
	}

	if (strstr(plaintext_string, "EASTNORTHEAST") != NULL) {
		eastnortheast_present = true;
		for (i = 0; i < 1000; i++) {
			printf("**** \'EASTNORTHEAST\' PRESENT!!! ****");
		}
	}

	printf("\n\n");
#endif

	// Single line summary of results for subsequent filtering and analysis. 

	if (dictionary_present_p) {
		printf("\n\n>>> %.2f, %d, %d, %s, ", best_score, n_words_found, cipher_type, ciphertext_file);
	} else {
		printf("\n\n>>> %.2f, %d, %s, ", best_score, cipher_type, ciphertext_file);
	}
	print_text(cipher_indices, cipher_len);
	printf(", ");
	print_text(best_plaintext_keyword, ALPHABET_SIZE);
	printf(", ");
	print_text(best_ciphertext_keyword, ALPHABET_SIZE);
	printf(", ");
	print_text(best_cycleword, best_cycleword_length);
	printf(", ");
	print_text(best_decrypted, cipher_len);
#if KRYPTOS
	if (berlin_present) {
		printf(", BERLIN");
	}
	if (clock_present) {
		printf(", CLOCK");
	}
	if (east_present) {
		printf(", EAST");
	}
	if (north_present) {
		printf(", NORTH");
	}
	if (berlinclock_present) {
		printf(", BERLINCLOCK");
	}
	if (eastnortheast_present) {
		printf(", EASTNORTHEAST");
	}
	printf("\n\n");
#endif

	free(ngram_data);

	return 1;
}



// Slippery stochastic shotgun restarted hill climber for Quagmire ciphers.

double quagmire_shotgun_hill_climber(
	int cipher_type, 
	int cipher_indices[], int cipher_len, 
	int crib_indices[], int crib_positions[], int n_cribs,
	int cycleword_len, int plaintext_keyword_len, int ciphertext_keyword_len, 
	int n_hill_climbs, int n_restarts,
	float *ngram_data, int ngram_size,
	int decrypted[MAX_CIPHER_LENGTH], int plaintext_keyword[ALPHABET_SIZE], 
	int ciphertext_keyword[ALPHABET_SIZE], int cycleword[ALPHABET_SIZE],
	double backtracking_probability, double keyword_permutation_probability, double slip_probability,
	float weight_ngram, float weight_crib, float weight_ioc, float weight_entropy, 
	bool variant, bool beaufort, bool verbose) {

	int i, j, n, indx, n_iterations, n_backtracks, n_explore, n_contradictions,
		local_plaintext_keyword_state[ALPHABET_SIZE], current_plaintext_keyword_state[ALPHABET_SIZE], 
		local_ciphertext_keyword_state[ALPHABET_SIZE], current_ciphertext_keyword_state[ALPHABET_SIZE], 
		best_plaintext_keyword_state[ALPHABET_SIZE], best_ciphertext_keyword_state[ALPHABET_SIZE],
		local_cycleword_state[MAX_CYCLEWORD_LEN], current_cycleword_state[MAX_CYCLEWORD_LEN], 
		best_cycleword_state[MAX_CYCLEWORD_LEN];
	double start_time, elapsed, n_iter_per_sec, best_score, local_score, current_score, ioc, chi, 
		entropy_score;
	bool perturbate_keyword_p, contradiction;

	if (cipher_type == VIGENERE) {
		cycleword_len = ALPHABET_SIZE;
	}

	n_iterations = 0;
	n_backtracks = 0;
	n_explore = 0;
	n_contradictions = 0;
	start_time = clock();

	best_score = 0.;

	for (n = 0; n < n_restarts; n++) {

		if (best_score > 0. && frand() < backtracking_probability) {
			// Backtrack to best state. 
			n_backtracks += 1;
			current_score = best_score;
			vec_copy(best_plaintext_keyword_state, current_plaintext_keyword_state, ALPHABET_SIZE);
			vec_copy(best_ciphertext_keyword_state, current_ciphertext_keyword_state, ALPHABET_SIZE);
			vec_copy(best_cycleword_state, current_cycleword_state, cycleword_len);
		} else {
			// Initialise random state.
			switch (cipher_type) {
				case VIGENERE:
					random_keyword(current_plaintext_keyword_state, ALPHABET_SIZE, plaintext_keyword_len);
					vec_copy(current_plaintext_keyword_state, current_ciphertext_keyword_state, ALPHABET_SIZE);
					vec_copy(current_plaintext_keyword_state, current_cycleword_state, ALPHABET_SIZE);
					break ;
				case QUAGMIRE_1:
					random_keyword(current_plaintext_keyword_state, ALPHABET_SIZE, plaintext_keyword_len);
					straight_alphabet(current_ciphertext_keyword_state, ALPHABET_SIZE);
					random_cycleword(current_cycleword_state, ALPHABET_SIZE, cycleword_len);
					break ;
				case QUAGMIRE_2:
					straight_alphabet(current_plaintext_keyword_state, ALPHABET_SIZE);
					random_keyword(current_ciphertext_keyword_state, ALPHABET_SIZE, ciphertext_keyword_len);
					random_cycleword(current_cycleword_state, ALPHABET_SIZE, cycleword_len);
					break ;
				case QUAGMIRE_3:
					random_keyword(current_plaintext_keyword_state, ALPHABET_SIZE, plaintext_keyword_len);
					vec_copy(current_plaintext_keyword_state, current_ciphertext_keyword_state, ALPHABET_SIZE);
					random_cycleword(current_cycleword_state, ALPHABET_SIZE, cycleword_len);
					break ;
				case QUAGMIRE_4:
					random_keyword(current_plaintext_keyword_state, ALPHABET_SIZE, plaintext_keyword_len);
					random_keyword(current_ciphertext_keyword_state, ALPHABET_SIZE, ciphertext_keyword_len);
					random_cycleword(current_cycleword_state, ALPHABET_SIZE, cycleword_len);
					break ;
				case BEAUFORT:
					plaintext_keyword_len = ALPHABET_SIZE;
					ciphertext_keyword_len = ALPHABET_SIZE;
					for (i = 0; i < ALPHABET_SIZE; i++) current_plaintext_keyword_state[i] = i;
					vec_copy(current_plaintext_keyword_state, current_ciphertext_keyword_state, ALPHABET_SIZE);
					random_cycleword(current_cycleword_state, ALPHABET_SIZE, cycleword_len);
					break ; 
			}

			current_score = state_score(cipher_indices, cipher_len, 
				crib_indices, crib_positions, n_cribs, 
				current_plaintext_keyword_state, current_ciphertext_keyword_state, 
				current_cycleword_state, cycleword_len, 
				variant, beaufort, 
				decrypted, ngram_data, ngram_size,
				weight_ngram, weight_crib, weight_ioc, weight_entropy);
		}

// The following are K4-specific hacks to manually set the ciphertext and plaintext keywords to KRYPTOS and/or KOMITET.

#if 0
	current_plaintext_keyword_state[0] = 0;
	current_plaintext_keyword_state[1] = 13;
	current_plaintext_keyword_state[2] = 6;
	current_plaintext_keyword_state[3] = 11;
	current_plaintext_keyword_state[4] = 4;
	current_plaintext_keyword_state[5] = 18;
	current_plaintext_keyword_state[6] = 1;
	current_plaintext_keyword_state[7] = 2;
	current_plaintext_keyword_state[8] = 3;
	current_plaintext_keyword_state[9] = 5;
	current_plaintext_keyword_state[10] = 7;
	current_plaintext_keyword_state[11] = 8;
	current_plaintext_keyword_state[12] = 9;
	current_plaintext_keyword_state[13] = 10;
	current_plaintext_keyword_state[14] = 12;
	current_plaintext_keyword_state[15] = 14;
	current_plaintext_keyword_state[16] = 15;
	current_plaintext_keyword_state[17] = 16;
	current_plaintext_keyword_state[18] = 17;
	current_plaintext_keyword_state[19] = 19;
	current_plaintext_keyword_state[20] = 20;
	current_plaintext_keyword_state[21] = 21;
	current_plaintext_keyword_state[22] = 22;
	current_plaintext_keyword_state[23] = 23;
	current_plaintext_keyword_state[24] = 24;
	current_plaintext_keyword_state[25] = 25;

	if (cipher_type == VIGENERE || cipher_type == BEAUFORT || cipher_type == QUAGMIRE_3) {
		vec_copy(current_plaintext_keyword_state, current_ciphertext_keyword_state, ALPHABET_SIZE);
	}
#endif

#if 0
	current_ciphertext_keyword_state[0] = 19;
	current_ciphertext_keyword_state[1] = 8;
	current_ciphertext_keyword_state[2] = 13;
	current_ciphertext_keyword_state[3] = 3;
	current_ciphertext_keyword_state[4] = 4;
	current_ciphertext_keyword_state[5] = 17;
	current_ciphertext_keyword_state[6] = 0;
	current_ciphertext_keyword_state[7] = 1;
	current_ciphertext_keyword_state[8] = 2;
	current_ciphertext_keyword_state[9] = 5;
	current_ciphertext_keyword_state[10] = 6;
	current_ciphertext_keyword_state[11] = 7;
	current_ciphertext_keyword_state[12] = 9;
	current_ciphertext_keyword_state[13] = 10;
	current_ciphertext_keyword_state[14] = 11;
	current_ciphertext_keyword_state[15] = 12;
	current_ciphertext_keyword_state[16] = 14;
	current_ciphertext_keyword_state[17] = 15;
	current_ciphertext_keyword_state[18] = 16;
	current_ciphertext_keyword_state[19] = 18;
	current_ciphertext_keyword_state[20] = 20;
	current_ciphertext_keyword_state[21] = 21;
	current_ciphertext_keyword_state[22] = 22;
	current_ciphertext_keyword_state[23] = 23;
	current_ciphertext_keyword_state[24] = 24;
	current_ciphertext_keyword_state[25] = 25;

	if (cipher_type == VIGENERE || cipher_type == BEAUFORT || cipher_type == QUAGMIRE_3) {
		vec_copy(current_ciphertext_keyword_state, current_plaintext_keyword_state, ALPHABET_SIZE);
	}
#endif

#if KOMITET_PT
	// KOMITE[T]
	current_plaintext_keyword_state[0] = 10;
	current_plaintext_keyword_state[1] = 14;
	current_plaintext_keyword_state[2] = 12;
	current_plaintext_keyword_state[3] = 8;
	current_plaintext_keyword_state[4] = 19;
	current_plaintext_keyword_state[5] = 4;
	current_plaintext_keyword_state[6] = 0;
	current_plaintext_keyword_state[7] = 1;
	current_plaintext_keyword_state[8] = 2;
	current_plaintext_keyword_state[9] = 3;
	current_plaintext_keyword_state[10] = 5;
	current_plaintext_keyword_state[11] = 6;
	current_plaintext_keyword_state[12] = 7;
	current_plaintext_keyword_state[13] = 9;
	current_plaintext_keyword_state[14] = 11;
	current_plaintext_keyword_state[15] = 13;
	current_plaintext_keyword_state[16] = 15;
	current_plaintext_keyword_state[17] = 16;
	current_plaintext_keyword_state[18] = 17;
	current_plaintext_keyword_state[19] = 18;
	current_plaintext_keyword_state[20] = 20;
	current_plaintext_keyword_state[21] = 21;
	current_plaintext_keyword_state[22] = 22;
	current_plaintext_keyword_state[23] = 23;
	current_plaintext_keyword_state[24] = 24;
	current_plaintext_keyword_state[25] = 25;

	if (cipher_type == VIGENERE || cipher_type == BEAUFORT || cipher_type == QUAGMIRE_3) {
		vec_copy(current_plaintext_keyword_state, current_ciphertext_keyword_state, ALPHABET_SIZE);
	}
#endif

#if KOMITET_CT
	// KOMITE[T]
	current_ciphertext_keyword_state[0] = 10;
	current_ciphertext_keyword_state[1] = 14;
	current_ciphertext_keyword_state[2] = 12;
	current_ciphertext_keyword_state[3] = 8;
	current_ciphertext_keyword_state[4] = 19;
	current_ciphertext_keyword_state[5] = 4;
	current_ciphertext_keyword_state[6] = 0;
	current_ciphertext_keyword_state[7] = 1;
	current_ciphertext_keyword_state[8] = 2;
	current_ciphertext_keyword_state[9] = 3;
	current_ciphertext_keyword_state[10] = 5;
	current_ciphertext_keyword_state[11] = 6;
	current_ciphertext_keyword_state[12] = 7;
	current_ciphertext_keyword_state[13] = 9;
	current_ciphertext_keyword_state[14] = 11;
	current_ciphertext_keyword_state[15] = 13;
	current_ciphertext_keyword_state[16] = 15;
	current_ciphertext_keyword_state[17] = 16;
	current_ciphertext_keyword_state[18] = 17;
	current_ciphertext_keyword_state[19] = 18;
	current_ciphertext_keyword_state[20] = 20;
	current_ciphertext_keyword_state[21] = 21;
	current_ciphertext_keyword_state[22] = 22;
	current_ciphertext_keyword_state[23] = 23;
	current_ciphertext_keyword_state[24] = 24;
	current_ciphertext_keyword_state[25] = 25;

	if (cipher_type == VIGENERE || cipher_type == BEAUFORT || cipher_type == QUAGMIRE_3) {
		vec_copy(current_ciphertext_keyword_state, current_plaintext_keyword_state, ALPHABET_SIZE);
	}
#endif


#if KRYPTOS_PT || KRYPTOS_PT_SCRAMBLE
	// KRYPTOS
	current_plaintext_keyword_state[0] = 10;
	current_plaintext_keyword_state[1] = 17;
	current_plaintext_keyword_state[2] = 24;
	current_plaintext_keyword_state[3] = 15;
	current_plaintext_keyword_state[4] = 19;
	current_plaintext_keyword_state[5] = 14;
	current_plaintext_keyword_state[6] = 18;
	current_plaintext_keyword_state[7] = 0; 
	current_plaintext_keyword_state[8] = 1;
	current_plaintext_keyword_state[9] = 2;
	current_plaintext_keyword_state[10] = 3;
	current_plaintext_keyword_state[11] = 4;
	current_plaintext_keyword_state[12] = 5; 
	current_plaintext_keyword_state[13] = 6; 
	current_plaintext_keyword_state[14] = 7; 
	current_plaintext_keyword_state[15] = 8;
	current_plaintext_keyword_state[16] = 9;
	current_plaintext_keyword_state[17] = 11; 
	current_plaintext_keyword_state[18] = 12;
	current_plaintext_keyword_state[19] = 13;
	current_plaintext_keyword_state[20] = 16;
	current_plaintext_keyword_state[21] = 20;
	current_plaintext_keyword_state[22] = 21; 
	current_plaintext_keyword_state[23] = 22;
	current_plaintext_keyword_state[24] = 23; 
	current_plaintext_keyword_state[25] = 25;

	if (cipher_type == VIGENERE || cipher_type == BEAUFORT || cipher_type == QUAGMIRE_3) {
		vec_copy(current_plaintext_keyword_state, current_ciphertext_keyword_state, ALPHABET_SIZE);
	}
#endif

#if KRYPTOS_CT
	// KRYPTOS
	current_ciphertext_keyword_state[0] = 10;
	current_ciphertext_keyword_state[1] = 17;
	current_ciphertext_keyword_state[2] = 24;
	current_ciphertext_keyword_state[3] = 15;
	current_ciphertext_keyword_state[4] = 19;
	current_ciphertext_keyword_state[5] = 14;
	current_ciphertext_keyword_state[6] = 18;
	current_ciphertext_keyword_state[7] = 0; 
	current_ciphertext_keyword_state[8] = 1;
	current_ciphertext_keyword_state[9] = 2;
	current_ciphertext_keyword_state[10] = 3;
	current_ciphertext_keyword_state[11] = 4;
	current_ciphertext_keyword_state[12] = 5; 
	current_ciphertext_keyword_state[13] = 6; 
	current_ciphertext_keyword_state[14] = 7; 
	current_ciphertext_keyword_state[15] = 8;
	current_ciphertext_keyword_state[16] = 9;
	current_ciphertext_keyword_state[17] = 11; 
	current_ciphertext_keyword_state[18] = 12;
	current_ciphertext_keyword_state[19] = 13;
	current_ciphertext_keyword_state[20] = 16;
	current_ciphertext_keyword_state[21] = 20;
	current_ciphertext_keyword_state[22] = 21; 
	current_ciphertext_keyword_state[23] = 22;
	current_ciphertext_keyword_state[24] = 23; 
	current_ciphertext_keyword_state[25] = 25;

	if (cipher_type == VIGENERE || cipher_type == BEAUFORT || cipher_type == QUAGMIRE_3) {
		vec_copy(current_ciphertext_keyword_state, current_plaintext_keyword_state, ALPHABET_SIZE);
	}
#endif

#if SOLUBLE_PT
	// SOLUB[L]E
	current_plaintext_keyword_state[0] = 18;
	current_plaintext_keyword_state[1] = 14;
	current_plaintext_keyword_state[2] = 11;
	current_plaintext_keyword_state[3] = 20;
	current_plaintext_keyword_state[4] = 1;
	current_plaintext_keyword_state[5] = 4;
	current_plaintext_keyword_state[6] = 0;
	current_plaintext_keyword_state[7] = 2; 
	current_plaintext_keyword_state[8] = 3;
	current_plaintext_keyword_state[9] = 5;
	current_plaintext_keyword_state[10] = 6;
	current_plaintext_keyword_state[11] = 7;
	current_plaintext_keyword_state[12] = 8; 
	current_plaintext_keyword_state[13] = 9; 
	current_plaintext_keyword_state[14] = 10; 
	current_plaintext_keyword_state[15] = 12;
	current_plaintext_keyword_state[16] = 13;
	current_plaintext_keyword_state[17] = 15; 
	current_plaintext_keyword_state[18] = 16;
	current_plaintext_keyword_state[19] = 17;
	current_plaintext_keyword_state[20] = 19;
	current_plaintext_keyword_state[21] = 21;
	current_plaintext_keyword_state[22] = 22; 
	current_plaintext_keyword_state[23] = 23;
	current_plaintext_keyword_state[24] = 24; 
	current_plaintext_keyword_state[25] = 25;

	if (cipher_type == VIGENERE || cipher_type == BEAUFORT || cipher_type == QUAGMIRE_3) {
		vec_copy(current_plaintext_keyword_state, current_ciphertext_keyword_state, ALPHABET_SIZE);
	}
#endif

#if SOLUBLE_CT
	// SOLUB[L]E
	current_ciphertext_keyword_state[0] = 18;
	current_ciphertext_keyword_state[1] = 14;
	current_ciphertext_keyword_state[2] = 11;
	current_ciphertext_keyword_state[3] = 20;
	current_ciphertext_keyword_state[4] = 1;
	current_ciphertext_keyword_state[5] = 4;
	current_ciphertext_keyword_state[6] = 0;
	current_ciphertext_keyword_state[7] = 2; 
	current_ciphertext_keyword_state[8] = 3;
	current_ciphertext_keyword_state[9] = 5;
	current_ciphertext_keyword_state[10] = 6;
	current_ciphertext_keyword_state[11] = 7;
	current_ciphertext_keyword_state[12] = 8; 
	current_ciphertext_keyword_state[13] = 9; 
	current_ciphertext_keyword_state[14] = 10; 
	current_ciphertext_keyword_state[15] = 12;
	current_ciphertext_keyword_state[16] = 13;
	current_ciphertext_keyword_state[17] = 15; 
	current_ciphertext_keyword_state[18] = 16;
	current_ciphertext_keyword_state[19] = 17;
	current_ciphertext_keyword_state[20] = 19;
	current_ciphertext_keyword_state[21] = 21;
	current_ciphertext_keyword_state[22] = 22; 
	current_ciphertext_keyword_state[23] = 23;
	current_ciphertext_keyword_state[24] = 24; 
	current_ciphertext_keyword_state[25] = 25;

	if (cipher_type == VIGENERE || cipher_type == BEAUFORT || cipher_type == QUAGMIRE_3) {
		vec_copy(current_ciphertext_keyword_state, current_plaintext_keyword_state, ALPHABET_SIZE);
	}
#endif

		perturbate_keyword_p = true;

		for (i = 0; i < n_hill_climbs; i++) {
				
			n_iterations += 1;

			// perturbate.
			vec_copy(current_plaintext_keyword_state, local_plaintext_keyword_state, ALPHABET_SIZE);
			vec_copy(current_ciphertext_keyword_state, local_ciphertext_keyword_state, ALPHABET_SIZE);
			vec_copy(current_cycleword_state, local_cycleword_state, cycleword_len);

			if (cipher_type != BEAUFORT && (perturbate_keyword_p || cipher_type == VIGENERE || frand() < keyword_permutation_probability)) {
				switch (cipher_type) {
					case VIGENERE:
						perturbate_keyword(local_plaintext_keyword_state, ALPHABET_SIZE, plaintext_keyword_len);
						vec_copy(local_plaintext_keyword_state, local_ciphertext_keyword_state, ALPHABET_SIZE);	
						vec_copy(local_plaintext_keyword_state, local_cycleword_state, ALPHABET_SIZE);
						break ; 
					case QUAGMIRE_1:
						perturbate_keyword(local_plaintext_keyword_state, ALPHABET_SIZE, plaintext_keyword_len);
						break ;
					case QUAGMIRE_2:
						perturbate_keyword(local_ciphertext_keyword_state, ALPHABET_SIZE, ciphertext_keyword_len);
						break ;
					case QUAGMIRE_3:
						perturbate_keyword(local_plaintext_keyword_state, ALPHABET_SIZE, plaintext_keyword_len);
						vec_copy(local_plaintext_keyword_state, local_ciphertext_keyword_state, ALPHABET_SIZE);
						break ;
					case QUAGMIRE_4:
						if (frand() < 0.5) {
							perturbate_keyword(local_plaintext_keyword_state, ALPHABET_SIZE, plaintext_keyword_len);
						} else {
							perturbate_keyword(local_ciphertext_keyword_state, ALPHABET_SIZE, ciphertext_keyword_len);
						}
						break ;
				}
			} else {
				perturbate_cycleword(local_cycleword_state, ALPHABET_SIZE, cycleword_len);
			}


// The following are K4-specific hacks to manually set the ciphertext and plaintext keywords to KRYPTOS and/or KOMITET.

#if 0
	local_plaintext_keyword_state[0] = 0;
	local_plaintext_keyword_state[1] = 13;
	local_plaintext_keyword_state[2] = 6;
	local_plaintext_keyword_state[3] = 11;
	local_plaintext_keyword_state[4] = 4;
	local_plaintext_keyword_state[5] = 18;
	local_plaintext_keyword_state[6] = 1;
	local_plaintext_keyword_state[7] = 2;
	local_plaintext_keyword_state[8] = 3;
	local_plaintext_keyword_state[9] = 5;
	local_plaintext_keyword_state[10] = 7;
	local_plaintext_keyword_state[11] = 8;
	local_plaintext_keyword_state[12] = 9;
	local_plaintext_keyword_state[13] = 10;
	local_plaintext_keyword_state[14] = 12;
	local_plaintext_keyword_state[15] = 14;
	local_plaintext_keyword_state[16] = 15;
	local_plaintext_keyword_state[17] = 16;
	local_plaintext_keyword_state[18] = 17;
	local_plaintext_keyword_state[19] = 19;
	local_plaintext_keyword_state[20] = 20;
	local_plaintext_keyword_state[21] = 21;
	local_plaintext_keyword_state[22] = 22;
	local_plaintext_keyword_state[23] = 23;
	local_plaintext_keyword_state[24] = 24;
	local_plaintext_keyword_state[25] = 25;

	if (cipher_type == VIGENERE || cipher_type == BEAUFORT || cipher_type == QUAGMIRE_3) {
		vec_copy(local_plaintext_keyword_state, local_ciphertext_keyword_state, ALPHABET_SIZE);
	}
#endif

#if 0
	local_ciphertext_keyword_state[0] = 19;
	local_ciphertext_keyword_state[1] = 8;
	local_ciphertext_keyword_state[2] = 13;
	local_ciphertext_keyword_state[3] = 3;
	local_ciphertext_keyword_state[4] = 4;
	local_ciphertext_keyword_state[5] = 17;
	local_ciphertext_keyword_state[6] = 0;
	local_ciphertext_keyword_state[7] = 1;
	local_ciphertext_keyword_state[8] = 2;
	local_ciphertext_keyword_state[9] = 5;
	local_ciphertext_keyword_state[10] = 6;
	local_ciphertext_keyword_state[11] = 7;
	local_ciphertext_keyword_state[12] = 9;
	local_ciphertext_keyword_state[13] = 10;
	local_ciphertext_keyword_state[14] = 11;
	local_ciphertext_keyword_state[15] = 12;
	local_ciphertext_keyword_state[16] = 14;
	local_ciphertext_keyword_state[17] = 15;
	local_ciphertext_keyword_state[18] = 16;
	local_ciphertext_keyword_state[19] = 18;
	local_ciphertext_keyword_state[20] = 20;
	local_ciphertext_keyword_state[21] = 21;
	local_ciphertext_keyword_state[22] = 22;
	local_ciphertext_keyword_state[23] = 23;
	local_ciphertext_keyword_state[24] = 24;
	local_ciphertext_keyword_state[25] = 25;

	if (cipher_type == VIGENERE || cipher_type == BEAUFORT || cipher_type == QUAGMIRE_3) {
		vec_copy(local_ciphertext_keyword_state, local_plaintext_keyword_state, ALPHABET_SIZE);
	}	
#endif

#if KOMITET_PT
	// KOMITE[T]
	local_plaintext_keyword_state[0] = 10;
	local_plaintext_keyword_state[1] = 14;
	local_plaintext_keyword_state[2] = 12;
	local_plaintext_keyword_state[3] = 8;
	local_plaintext_keyword_state[4] = 19;
	local_plaintext_keyword_state[5] = 4;
	local_plaintext_keyword_state[6] = 0;
	local_plaintext_keyword_state[7] = 1;
	local_plaintext_keyword_state[8] = 2;
	local_plaintext_keyword_state[9] = 3;
	local_plaintext_keyword_state[10] = 5;
	local_plaintext_keyword_state[11] = 6;
	local_plaintext_keyword_state[12] = 7;
	local_plaintext_keyword_state[13] = 9;
	local_plaintext_keyword_state[14] = 11;
	local_plaintext_keyword_state[15] = 13;
	local_plaintext_keyword_state[16] = 15;
	local_plaintext_keyword_state[17] = 16;
	local_plaintext_keyword_state[18] = 17;
	local_plaintext_keyword_state[19] = 18;
	local_plaintext_keyword_state[20] = 20;
	local_plaintext_keyword_state[21] = 21;
	local_plaintext_keyword_state[22] = 22;
	local_plaintext_keyword_state[23] = 23;
	local_plaintext_keyword_state[24] = 24;
	local_plaintext_keyword_state[25] = 25;

	if (cipher_type == VIGENERE || cipher_type == BEAUFORT || cipher_type == QUAGMIRE_3) {
		vec_copy(local_plaintext_keyword_state, local_ciphertext_keyword_state, ALPHABET_SIZE);
	}
#endif

#if KOMITET_CT
	// KOMITE[T]
	local_ciphertext_keyword_state[0] = 10;
	local_ciphertext_keyword_state[1] = 14;
	local_ciphertext_keyword_state[2] = 12;
	local_ciphertext_keyword_state[3] = 8;
	local_ciphertext_keyword_state[4] = 19;
	local_ciphertext_keyword_state[5] = 4;
	local_ciphertext_keyword_state[6] = 0;
	local_ciphertext_keyword_state[7] = 1;
	local_ciphertext_keyword_state[8] = 2;
	local_ciphertext_keyword_state[9] = 3;
	local_ciphertext_keyword_state[10] = 5;
	local_ciphertext_keyword_state[11] = 6;
	local_ciphertext_keyword_state[12] = 7;
	local_ciphertext_keyword_state[13] = 9;
	local_ciphertext_keyword_state[14] = 11;
	local_ciphertext_keyword_state[15] = 13;
	local_ciphertext_keyword_state[16] = 15;
	local_ciphertext_keyword_state[17] = 16;
	local_ciphertext_keyword_state[18] = 17;
	local_ciphertext_keyword_state[19] = 18;
	local_ciphertext_keyword_state[20] = 20;
	local_ciphertext_keyword_state[21] = 21;
	local_ciphertext_keyword_state[22] = 22;
	local_ciphertext_keyword_state[23] = 23;
	local_ciphertext_keyword_state[24] = 24;
	local_ciphertext_keyword_state[25] = 25;

	if (cipher_type == VIGENERE || cipher_type == BEAUFORT || cipher_type == QUAGMIRE_3) {
		vec_copy(local_ciphertext_keyword_state, local_plaintext_keyword_state, ALPHABET_SIZE);
	}
#endif


#if KRYPTOS_PT
	// KRYPTOS
	local_plaintext_keyword_state[0] = 10;
	local_plaintext_keyword_state[1] = 17;
	local_plaintext_keyword_state[2] = 24;
	local_plaintext_keyword_state[3] = 15;
	local_plaintext_keyword_state[4] = 19;
	local_plaintext_keyword_state[5] = 14;
	local_plaintext_keyword_state[6] = 18;
	local_plaintext_keyword_state[7] = 0; 
	local_plaintext_keyword_state[8] = 1;
	local_plaintext_keyword_state[9] = 2;
	local_plaintext_keyword_state[10] = 3;
	local_plaintext_keyword_state[11] = 4;
	local_plaintext_keyword_state[12] = 5; 
	local_plaintext_keyword_state[13] = 6; 
	local_plaintext_keyword_state[14] = 7; 
	local_plaintext_keyword_state[15] = 8;
	local_plaintext_keyword_state[16] = 9;
	local_plaintext_keyword_state[17] = 11; 
	local_plaintext_keyword_state[18] = 12;
	local_plaintext_keyword_state[19] = 13;
	local_plaintext_keyword_state[20] = 16;
	local_plaintext_keyword_state[21] = 20;
	local_plaintext_keyword_state[22] = 21; 
	local_plaintext_keyword_state[23] = 22;
	local_plaintext_keyword_state[24] = 23; 
	local_plaintext_keyword_state[25] = 25;

	if (cipher_type == VIGENERE || cipher_type == BEAUFORT || cipher_type == QUAGMIRE_3) {
		vec_copy(local_plaintext_keyword_state, local_ciphertext_keyword_state, ALPHABET_SIZE);
	}
#endif

#if KRYPTOS_CT
	// KRYPTOS
	local_ciphertext_keyword_state[0] = 10;
	local_ciphertext_keyword_state[1] = 17;
	local_ciphertext_keyword_state[2] = 24;
	local_ciphertext_keyword_state[3] = 15;
	local_ciphertext_keyword_state[4] = 19;
	local_ciphertext_keyword_state[5] = 14;
	local_ciphertext_keyword_state[6] = 18;
	local_ciphertext_keyword_state[7] = 0; 
	local_ciphertext_keyword_state[8] = 1;
	local_ciphertext_keyword_state[9] = 2;
	local_ciphertext_keyword_state[10] = 3;
	local_ciphertext_keyword_state[11] = 4;
	local_ciphertext_keyword_state[12] = 5; 
	local_ciphertext_keyword_state[13] = 6; 
	local_ciphertext_keyword_state[14] = 7; 
	local_ciphertext_keyword_state[15] = 8;
	local_ciphertext_keyword_state[16] = 9;
	local_ciphertext_keyword_state[17] = 11; 
	local_ciphertext_keyword_state[18] = 12;
	local_ciphertext_keyword_state[19] = 13;
	local_ciphertext_keyword_state[20] = 16;
	local_ciphertext_keyword_state[21] = 20;
	local_ciphertext_keyword_state[22] = 21; 
	local_ciphertext_keyword_state[23] = 22;
	local_ciphertext_keyword_state[24] = 23; 
	local_ciphertext_keyword_state[25] = 25;

	if (cipher_type == VIGENERE || cipher_type == BEAUFORT || cipher_type == QUAGMIRE_3) {
		vec_copy(local_ciphertext_keyword_state, local_plaintext_keyword_state, ALPHABET_SIZE);
	}
#endif

#if SOLUBLE_PT
	// SOLUB[L]E
	local_plaintext_keyword_state[0] = 18;
	local_plaintext_keyword_state[1] = 14;
	local_plaintext_keyword_state[2] = 11;
	local_plaintext_keyword_state[3] = 20;
	local_plaintext_keyword_state[4] = 1;
	local_plaintext_keyword_state[5] = 4;
	local_plaintext_keyword_state[6] = 0;
	local_plaintext_keyword_state[7] = 2; 
	local_plaintext_keyword_state[8] = 3;
	local_plaintext_keyword_state[9] = 5;
	local_plaintext_keyword_state[10] = 6;
	local_plaintext_keyword_state[11] = 7;
	local_plaintext_keyword_state[12] = 8; 
	local_plaintext_keyword_state[13] = 9; 
	local_plaintext_keyword_state[14] = 10; 
	local_plaintext_keyword_state[15] = 12;
	local_plaintext_keyword_state[16] = 13;
	local_plaintext_keyword_state[17] = 15; 
	local_plaintext_keyword_state[18] = 16;
	local_plaintext_keyword_state[19] = 17;
	local_plaintext_keyword_state[20] = 19;
	local_plaintext_keyword_state[21] = 21;
	local_plaintext_keyword_state[22] = 22; 
	local_plaintext_keyword_state[23] = 23;
	local_plaintext_keyword_state[24] = 24; 
	local_plaintext_keyword_state[25] = 25;

	if (cipher_type == VIGENERE || cipher_type == BEAUFORT || cipher_type == QUAGMIRE_3) {
		vec_copy(local_plaintext_keyword_state, local_ciphertext_keyword_state, ALPHABET_SIZE);
	}
#endif

#if SOLUBLE_CT
	// SOLUB[L]E
	local_ciphertext_keyword_state[0] = 18;
	local_ciphertext_keyword_state[1] = 14;
	local_ciphertext_keyword_state[2] = 11;
	local_ciphertext_keyword_state[3] = 20;
	local_ciphertext_keyword_state[4] = 1;
	local_ciphertext_keyword_state[5] = 4;
	local_ciphertext_keyword_state[6] = 0;
	local_ciphertext_keyword_state[7] = 2; 
	local_ciphertext_keyword_state[8] = 3;
	local_ciphertext_keyword_state[9] = 5;
	local_ciphertext_keyword_state[10] = 6;
	local_ciphertext_keyword_state[11] = 7;
	local_ciphertext_keyword_state[12] = 8; 
	local_ciphertext_keyword_state[13] = 9; 
	local_ciphertext_keyword_state[14] = 10; 
	local_ciphertext_keyword_state[15] = 12;
	local_ciphertext_keyword_state[16] = 13;
	local_ciphertext_keyword_state[17] = 15; 
	local_ciphertext_keyword_state[18] = 16;
	local_ciphertext_keyword_state[19] = 17;
	local_ciphertext_keyword_state[20] = 19;
	local_ciphertext_keyword_state[21] = 21;
	local_ciphertext_keyword_state[22] = 22; 
	local_ciphertext_keyword_state[23] = 23;
	local_ciphertext_keyword_state[24] = 24; 
	local_ciphertext_keyword_state[25] = 25;

	if (cipher_type == VIGENERE || cipher_type == BEAUFORT || cipher_type == QUAGMIRE_3) {
		vec_copy(local_ciphertext_keyword_state, local_plaintext_keyword_state, ALPHABET_SIZE);
	}
#endif

			if (cipher_type != VIGENERE && cipher_type != BEAUFORT) {
				perturbate_keyword_p = false;
				contradiction = constrain_cycleword(cipher_indices, cipher_len, crib_indices, 
					crib_positions, n_cribs, 
					local_plaintext_keyword_state, local_ciphertext_keyword_state, 
					local_cycleword_state, cycleword_len, variant, verbose);

				if (contradiction) {
					// Cycleword contradiction - must perturbate keyword(s). 
					n_contradictions += 1; 
					perturbate_keyword_p = true; 
				}
			}

			// Compute score. 
			local_score = state_score(cipher_indices, cipher_len, 
				crib_indices, crib_positions, n_cribs, 
				local_plaintext_keyword_state, local_ciphertext_keyword_state, 
				local_cycleword_state, cycleword_len, 
				variant, beaufort, 
				decrypted, ngram_data, ngram_size,
				weight_ngram, weight_crib, weight_ioc, weight_entropy);

#if 0
			printf("\nlocal_score = %.4f\n", local_score);
			print_text(decrypted, cipher_len);
			printf("\n");
			print_text(local_plaintext_keyword_state, ALPHABET_SIZE);
			printf("\n");
			print_text(local_ciphertext_keyword_state, ALPHABET_SIZE);
			printf("\n");
			print_text(local_cycleword_state, cycleword_len);
			printf("\n");
#endif

			if (local_score > current_score) {
				// printf("improvement\n");
				current_score = local_score;
				vec_copy(local_plaintext_keyword_state, current_plaintext_keyword_state, ALPHABET_SIZE);
				vec_copy(local_ciphertext_keyword_state, current_ciphertext_keyword_state, ALPHABET_SIZE);
				vec_copy(local_cycleword_state, current_cycleword_state, cycleword_len);
			} else if (frand() < slip_probability) {
				// printf("exploring\n");
				n_explore += 1;
				current_score = local_score;
				vec_copy(local_plaintext_keyword_state, current_plaintext_keyword_state, ALPHABET_SIZE);
				vec_copy(local_ciphertext_keyword_state, current_ciphertext_keyword_state, ALPHABET_SIZE);
				vec_copy(local_cycleword_state, current_cycleword_state, cycleword_len);
			}

			if (current_score > best_score) {
				best_score = current_score;
				vec_copy(current_plaintext_keyword_state, best_plaintext_keyword_state, ALPHABET_SIZE);
				vec_copy(current_ciphertext_keyword_state, best_ciphertext_keyword_state, ALPHABET_SIZE);
				vec_copy(current_cycleword_state, best_cycleword_state, cycleword_len);
				if (verbose) {

					if (variant) {
						quagmire_encrypt(decrypted, cipher_indices, cipher_len, 
							best_plaintext_keyword_state, best_ciphertext_keyword_state, 
							best_cycleword_state, cycleword_len, beaufort);
					} else {
						quagmire_decrypt(decrypted, cipher_indices, cipher_len, 
							best_plaintext_keyword_state, best_ciphertext_keyword_state, 
							best_cycleword_state, cycleword_len, beaufort);
					}

					ioc = index_of_coincidence(decrypted, cipher_len);
					chi = chi_squared(decrypted, cipher_len);
					entropy_score = entropy(decrypted, cipher_len);

					elapsed = ((double) clock() - start_time)/CLOCKS_PER_SEC;
					n_iter_per_sec = ((double) n_iterations)/elapsed;

					printf("\n%.2f\t[sec]\n", elapsed);
					printf("%.0fK\t[it/sec]\n", 1.e-3*n_iter_per_sec);
					printf("%d\t[backtracks]\n", n_backtracks);
					printf("%d\t[restarts]\n", n);
					printf("%d\t[iterations]\n", i);
					printf("%d\t[slips]\n", n_explore);
					printf("%.2f\t[contradiction pct]\n", ((double) n_contradictions)/n_iterations);
					printf("%.4f\t[IOC]\n", ioc);
					printf("%.4f\t[entropy]\n", entropy_score);
					printf("%.2f\t[chi-squared]\n", chi);
					printf("%.2f\t[score]\n", best_score);
					print_text(best_plaintext_keyword_state, ALPHABET_SIZE);
					printf("\n");
					print_text(best_ciphertext_keyword_state, ALPHABET_SIZE);
					printf("\n");
					print_text(best_cycleword_state, cycleword_len);
					printf("\n");

					// Display Quagmire tablau. 
					printf("\n");
					for (i = 0; i < cycleword_len; i++) {
						for (j = 0; j < ALPHABET_SIZE; j++) {
							indx = (j + best_cycleword_state[i]) % ALPHABET_SIZE;
							printf("%c", best_ciphertext_keyword_state[indx] + 'A');
						}
						printf("\n");
					}
					printf("\n");

					print_text(decrypted, cipher_len);
					printf("\n");
					fflush(stdout);
				}
			}
		}
	}

	vec_copy(best_plaintext_keyword_state, plaintext_keyword, ALPHABET_SIZE);
	vec_copy(best_ciphertext_keyword_state, ciphertext_keyword, ALPHABET_SIZE);
	vec_copy(best_cycleword_state, cycleword, cycleword_len);

	if (variant) {
		quagmire_encrypt(decrypted, cipher_indices, cipher_len, 
						best_plaintext_keyword_state, best_ciphertext_keyword_state, 
						best_cycleword_state, cycleword_len, beaufort);
	} else {
		quagmire_decrypt(decrypted, cipher_indices, cipher_len, 
						best_plaintext_keyword_state, best_ciphertext_keyword_state, 
						best_cycleword_state, cycleword_len, beaufort);
	}

	return best_score;
}



// Does the ciphertext trivially satisfy the cribs? For a given cycleword length, there 
// should be a one-to-one mapping between the ciphertext and the plaintext. 

bool cribs_satisfied_p(int cipher_indices[], int cipher_len, int crib_indices[], 
	int crib_positions[], int n_cribs, int cycleword_len, bool verbose) {

	int i, j, k, ii, jj, total, column_length, ciphertext_column_indices[MAX_CIPHER_LENGTH], 
		ciphertext_column[MAX_CIPHER_LENGTH], crib_frequencies[ALPHABET_SIZE][ALPHABET_SIZE];

	// Check cribs are present. 

	if (n_cribs == 0) {
		return true;
	}

	for (j = 0; j < cycleword_len; j++) {

		if (verbose) {
			printf("\nCOLUMN = %d \n", j);
		}

		// Extract column. 

		k = 0;
		while (cycleword_len*k + j < cipher_len) {
			ciphertext_column_indices[k] = cycleword_len*k + j;
			ciphertext_column[k] = cipher_indices[ciphertext_column_indices[k]];
			k++;
		}

		column_length = k;

		// Reset frequencies to zero. 

		for (i = 0; i < ALPHABET_SIZE; i++) {
			for (k = 0; k < ALPHABET_SIZE; k++) {
				crib_frequencies[i][k] = 0;
			}
		}

		// Check column satisfies the cribs. 

		for (i = 0; i < n_cribs; i++) {

			// Check the crib corresponds to exactly 1 ciphertext symbol. 

			for (k = 0; k < column_length; k++) {

				if (crib_positions[i] == ciphertext_column_indices[k]) {

					if (verbose) {
						printf("CT = %c, PT = %c\n", ciphertext_column[k] + 'A', crib_indices[i] + 'A');
					}

					crib_frequencies[crib_indices[i]][ciphertext_column[k]] = 1;

					// Check for clash in rows.
					for (ii = 0; ii < ALPHABET_SIZE; ii++) {
						total = 0;
						for (jj = 0; jj < ALPHABET_SIZE; jj++) {
							total += crib_frequencies[ii][jj];
							if (total > 1) {
								printf("\n\nContradiction at col %d, crib char %c\n\n", j, crib_indices[i] + 'A');
								return false;
							}
						}
					}

					// Check for clash in cols. 
					for (jj = 0; jj < ALPHABET_SIZE; jj++) {
						total = 0;
						for (ii = 0; ii < ALPHABET_SIZE; ii++) {
							total += crib_frequencies[ii][jj];
							if (total > 1) {
								printf("\n\nContradiction at col %d, crib char %c\n\n", j, crib_indices[i] + 'A');
								return false;
							}
						}
					}					
				}
			}
		}

	}

	return true;
}



// For a given candidate keyword - constrain the cycleword based on the cribs. If multiple 
// cribs produce conflicting cycleword rotations, then we have a conflict and must reject
// the keyword. 

bool constrain_cycleword(int cipher_indices[], int cipher_len, 
	int crib_indices[], int crib_positions[], int n_cribs, 
	int plaintext_keyword_indices[], int ciphertext_keyword_indices[], 
	int cycleword_indices[], int cycleword_len, 
	bool variant, bool verbose) {

	int i, j, k, crib_char, ciphertext_char, posn_keyword, posn_cycleword, 
		indx, crib_cyclewords[MAX_CYCLEWORD_LEN];

	// Check cribs are present. 

	if (n_cribs == 0) {
		return false; // No contradiction. 
	}

	// Set cycleword rotations to inactive. This is used to check for a contradiction 
	// and thus reject the candidate keyword. 
	for (i = 0; i < cycleword_len; i++) {
		crib_cyclewords[i] = INACTIVE; 
	}

	for (i = 0; i < cycleword_len; i++) {

		// Rotate/modify cycleword based on (plaintext and ciphertext) keyword(s) and crib. 

		for (j = 0; j < n_cribs; j++) {
			if (crib_positions[j]%cycleword_len == i) {

				crib_char = crib_indices[j];
				ciphertext_char = cipher_indices[crib_positions[j]];

				if (variant) {
					// Find position of ciphertext_char in the ciphertext keyword. 
					
					for (k = 0; k < ALPHABET_SIZE; k++) {
						if (plaintext_keyword_indices[k] == ciphertext_char) {
							posn_keyword = k;
							break ;
						}
					}

					// Find position of crib_char in plaintext keyword. 

					for (k = 0; k < ALPHABET_SIZE; k++) {
						if (ciphertext_keyword_indices[k] == crib_char) {
							posn_cycleword = k;
							break ;
						}
					}

					// Compute cycleword rotation. 

					indx = (posn_cycleword - posn_keyword)%ALPHABET_SIZE;
					if (indx < 0) indx += ALPHABET_SIZE;
				} else {
					// Find position of ciphertext_char in the ciphertext keyword. 
					
					for (k = 0; k < ALPHABET_SIZE; k++) {
						if (ciphertext_keyword_indices[k] == ciphertext_char) {
							posn_keyword = k;
							break ;
						}
					}

					// Find position of crib_char in plaintext keyword. 

					for (k = 0; k < ALPHABET_SIZE; k++) {
						if (plaintext_keyword_indices[k] == crib_char) {
							posn_cycleword = k;
							break ;
						}
					}

					// Compute cycleword rotation. 

					indx = (posn_keyword - posn_cycleword)%ALPHABET_SIZE;
					if (indx < 0) indx += ALPHABET_SIZE;					
				}

				// Has this cycleword position been previously set? 

				if (crib_cyclewords[i] == INACTIVE) {
					if (false) {
						printf("cycleword char %c at %d\n", plaintext_keyword_indices[indx] + 'A', i);
					}
					crib_cyclewords[i] = plaintext_keyword_indices[indx];
					cycleword_indices[i] = plaintext_keyword_indices[indx]; 
				} else if (crib_cyclewords[i] != plaintext_keyword_indices[indx]) { // Otherwise, do we have a contradiction? 
					if (false) {
						printf("\n\nContradiction at crib %c, posn %d; rejecting keyword ", 
							crib_indices[j] + 'A', 
							crib_positions[j]);
						print_text(plaintext_keyword_indices, ALPHABET_SIZE);
						printf("\n");
						print_text(ciphertext_keyword_indices, ALPHABET_SIZE);
						printf("\n");
					}
					return true; 
				}

			}

		}

	}

	return false;
}



// Score candidate cipher solution. 

double state_score(int cipher_indices[], int cipher_len, 
			int crib_indices[], int crib_positions[], int n_cribs, 
			int plaintext_keyword_state[], int ciphertext_keyword_state[], 
			int cycleword_state[], int cycleword_len, 
			bool variant, bool beaufort, 
			int decrypted[], 
			float *ngram_data, int ngram_size, 
			float weight_ngram, float weight_crib, float weight_ioc, float weight_entropy) {

	double score, decrypted_ngram_score, decrypted_crib_score;

	// Decrypt cipher using the candidate keyword and cycleword. 

	if (variant) {
		quagmire_encrypt(decrypted, cipher_indices, cipher_len, 
			plaintext_keyword_state, ciphertext_keyword_state, 
			cycleword_state, cycleword_len, beaufort);
	} else {
		quagmire_decrypt(decrypted, cipher_indices, cipher_len, 
			plaintext_keyword_state, ciphertext_keyword_state, 
			cycleword_state, cycleword_len, beaufort);
	}

	// n-gram score. 

	decrypted_ngram_score = ngram_score(decrypted, cipher_len, ngram_data, ngram_size);

	// crib score. 

	decrypted_crib_score = crib_score(decrypted, cipher_len, crib_indices, crib_positions, n_cribs);

	// Expected IOC. 

	double mean_english_ioc, ioc_score;
	mean_english_ioc = 1.742;
	ioc_score = ALPHABET_SIZE*index_of_coincidence(decrypted, cipher_len);	
	// ioc_score = 1./(1. + pow(ioc_score - mean_english_ioc, 2));
	ioc_score = exp(-pow(ioc_score - mean_english_ioc, 2));

	// Expected entropy. 

	double mean_english_entropy, entropy_score; 
	mean_english_entropy = 2.85;
	entropy_score = entropy(decrypted, cipher_len);	
	// entropy_score = 1./(1. + pow(entropy_score - mean_english_entropy, 2));
	entropy_score = exp(-pow(entropy_score - mean_english_entropy, 2));

	// printf("\n%.4f, %.4f, %.4f, %.4f", decrypted_ngram_score, decrypted_crib_score, ioc_score, entropy_score);

	score = weight_ngram*decrypted_ngram_score + 
			weight_crib*decrypted_crib_score + 
			weight_ioc*ioc_score + 
			weight_entropy*entropy_score;

	score /= weight_ngram + weight_crib + weight_ioc + weight_entropy;

	score /= 3.41; // score for example cipher of length 97 (using the current weighting scheme). 

	return score;
}




// Entropy. 

double entropy(int text[], int len) {

	int frequencies[ALPHABET_SIZE];
	double entropy = 0., freq;

	// Count frequencies of each plaintext letter. 
	tally(text, len, frequencies, ALPHABET_SIZE);

	for (int i = 0; i < ALPHABET_SIZE; i++) {
		if (frequencies[i] > 0) {
			freq = ((double) frequencies[i])/len;
			entropy -= freq*log(freq);
		}
	}
	// printf("entropy = %.4f\n", entropy);

	return entropy;
}



// Chi-squared score. 

double chi_squared(int plaintext[], int len) {

	int i, counts[ALPHABET_SIZE];
	double frequency, chi2 = 0.;

	tally(plaintext, len, counts, ALPHABET_SIZE);

	for (i = 0; i < ALPHABET_SIZE; i++) {
		frequency = ((double) counts[i])/len;
		// printf("%d, %.4f, %.4f\n", i, frequency, english_monograms[i]);
		chi2 += pow(frequency - english_monograms[i], 2)/english_monograms[i];
	}

	return chi2;
}




// Score for known plaintext. (Naive - not using symmetry of the Vigenere encryption.)

double crib_score(int text[], int len, int crib_indices[], int crib_positions[], int n_cribs) {

	if (n_cribs == 0) return 0.;

	int n_matches = 0;

	for (int i = 0; i < n_cribs; i++) {
		if (text[crib_positions[i]] == crib_indices[i]) {
			n_matches += 1;
		}
	}

	return ((double) n_matches)/((double) n_cribs);
}



// Score a plaintext based on ngram frequencies. 


double ngram_score(int decrypted[], int cipher_len, float *ngram_data, int ngram_size) {

	int index, base;
	double score = 0.;

	for (int i = 0; i < cipher_len - ngram_size; i++) {

		index = 0;
		base = 1;

		for (int j = 0; j < ngram_size; j++) {
			index += decrypted[i + j]*base;
			base *= ALPHABET_SIZE;
		}

		score += ngram_data[index];
	}

	// Normalise to cipher length and n-gram size. 

	score = pow(ALPHABET_SIZE,ngram_size)*score/(cipher_len - ngram_size);

	return score;
}


// Old, slow ngram score routine. 

double ngram_score_slow(int decrypted[], int cipher_len, float *ngram_data, int ngram_size) {

	int indx, ngram[MAX_NGRAM_SIZE];
	double score = 0.;

	for (int i = 0; i < cipher_len - ngram_size; i++) {

		// printf("\n");
		// Extract slice decrypted[i : i + ngram_size].
		for (int j = 0; j < ngram_size; j++) {
			ngram[j] = decrypted[i + j];
			// printf("%c", ngram[j] + 'A');
		}
		// printf("\t");

		indx = ngram_index_int(ngram, ngram_size);
		// printf("%.12f", ngram_data[indx]);
		score += ngram_data[indx];
	}

	return pow(ALPHABET_SIZE,ngram_size)*score/(cipher_len - ngram_size);
}



// Given a ciphertext, keyword and cycleword (all in index form), compute the 
// Quagmire 4 decryption. 

void quagmire_decrypt(int decrypted[], int cipher_indices[], int cipher_len, 
	int plaintext_keyword_indices[], int ciphertext_keyword_indices[], 
	int cycleword_indices[], int cycleword_len, bool beaufort) {
	
	int i, j, posn_keyword, posn_cycleword, indx, ct_indx, cw_indx; 

	for (i = 0; i < cipher_len; i++) {

		// Find position of ciphertext char in ciphertext key. 
		for (j = 0; j < ALPHABET_SIZE; j++) {
			ct_indx = ciphertext_keyword_indices[j];
			if (cipher_indices[i] == ct_indx) {
				posn_keyword = j;
				break ;
			}
		}

		// Find the position of cycleword char in keyword. 
		for (j = 0; j < ALPHABET_SIZE; j++) {
			cw_indx = cycleword_indices[i%cycleword_len];
			if (beaufort) {
				cw_indx = ALPHABET_SIZE - cw_indx - 1; // Atbash
			}
			if (cw_indx == ciphertext_keyword_indices[j]) {
				posn_cycleword = j; 
				break ;
			}
		}

		indx = (posn_keyword - posn_cycleword)%ALPHABET_SIZE;
		if (indx < 0) indx += ALPHABET_SIZE;
		decrypted[i] = plaintext_keyword_indices[indx];
		if (beaufort) {
			decrypted[i] = ALPHABET_SIZE - decrypted[i] - 1; // Atbash
		}
	}

	return ;
}



// Given a ciphertext, keyword and cycleword (all in index form), compute the 
// Quagmire 4 encryption. 

void quagmire_encrypt(int encrypted[], int plaintext_indices[], int cipher_len, 
	int plaintext_keyword_indices[], int ciphertext_keyword_indices[], 
	int cycleword_indices[], int cycleword_len, bool beaufort) {
	
	int i, j, posn_keyword, posn_cycleword, indx, pt_indx, cw_indx;

	for (i = 0; i < cipher_len; i++) {

		// Find position of plaintext char in the plaintext keyword. 
		for (j = 0; j < ALPHABET_SIZE; j++) {
			pt_indx = plaintext_indices[i];
			if (pt_indx == plaintext_keyword_indices[j]) {
				posn_keyword = j;
				break ;
			}
		}

		// Find the position of cycleword char in the ciphertext keyword. 
		for (j = 0; j < ALPHABET_SIZE; j++) {
			cw_indx = cycleword_indices[i%cycleword_len];
			if (beaufort) {
				cw_indx = ALPHABET_SIZE - cw_indx - 1; // Atbash
			}
			if (cw_indx == ciphertext_keyword_indices[j]) {
				posn_cycleword = j; 
				break ;
			}
		}

		indx = (posn_keyword + posn_cycleword)%ALPHABET_SIZE;
		if (indx < 0) indx += ALPHABET_SIZE;
		encrypted[i] = ciphertext_keyword_indices[indx];
		if (beaufort) {
			encrypted[i] = ALPHABET_SIZE - encrypted[i] - 1; // Atbash
		}
	}

	return ;
}



// perturbate a cycleword. 

void perturbate_cycleword(int state[], int max, int len) {

	int i; 
	i = rand_int(0, len);
	state[i] = rand_int(0, max);
}



// perturbate a key - Ref: http://www.mountainvistasoft.com/cryptoden/articles/Q3%20Keyspace.pdf

void perturbate_keyword(int state[], int len, int keyword_len) {

	int i, j, k, l, temp;

	if (frand() < 0.2) {
		// Once in 5, swap two letters within the keyspace.  
#if KRYPTOS_PT_SCRAMBLE
		i = rand_int(7, keyword_len);
		j = rand_int(7, keyword_len);
#else
		i = rand_int(0, keyword_len);
		j = rand_int(0, keyword_len);
#endif
		temp = state[i];
		state[i] = state[j];
		state[j] = temp;
	} else {
		// Four times in 5, swap a letter in the keyspace with 
		// a letter outside and remake the letters following the 
		// keyspace in normal order.

#if KRYPTOS_PT_SCRAMBLE
		i = rand_int(7, len);	
		j = rand_int(7, len);	
#else
#if FREQUENCY_WEIGHTED_SELECTION
		i = rand_int_frequency_weighted(state, 0, keyword_len);
		j = rand_int_frequency_weighted(state, keyword_len, len);
#else
		i = rand_int(0, keyword_len);
		j = rand_int(keyword_len, len);
#endif
#endif

		// printf("\ni,j = %d,%d\n", i, j);

		temp = state[i];
		state[i] = state[j];

		// Re-order - delete state[j]. 

		for (k = j + 1; k < len; k++) {
			state[k - 1] = state[k];
		}
		
		// Re-order - insert state[i]. 
		for (k = keyword_len; k < len; k++) {
			// Find insertion point. 
			if (state[k] > temp || k == len - 1) {
				// Shunt along. 
				for (l = len - 1; l > k; l--) {
					state[l] = state[l - 1];
				}
				// Insert. 
				state[k] = temp;
				break ;
			}
		}

	}

	return ;
}



// Random keyword initialisation routine. 

void random_keyword(int keyword[], int len, int keyword_len) {

	int i, j, candidate, indx, n_chars;
	bool distinct, present;

	// Get keyword_len distinct letters in [0 - ALPHABET_SIZE). 

	n_chars = 0;
	while (n_chars < keyword_len) {

		distinct = true;
		candidate = rand_int(0, ALPHABET_SIZE);

		for (i = 0; i < n_chars; i++) {
			if (keyword[i] == candidate) {
				distinct = false;
				break ;
			}
		}

		if (distinct) {
			keyword[n_chars++] = candidate;
		}
	}

	// Pad out the rest of the chars. Eg. if we have "KRYPTOS", then here we 
	// generate "ABCDEFGHIJLMNQUVWXZ" (in index form). 

	indx = keyword_len;
	for (i = 0; i < ALPHABET_SIZE; i++) {

		present = false;
		for (j = 0; j < keyword_len; j++) {
			if (keyword[j] == i) {
				present = true; 
				break ;
			}
		}

		if (! present) {
			keyword[indx++] = i;
		}
	}

	return ;
}



void random_cycleword(int cycleword[], int max, int keyword_len) {

	for (int i = 0; i < keyword_len; i++) {
		cycleword[i] = rand_int(0, max);
	}

	return ;
}



// English monogram frequency-weighted pseudo-random selection. 

int rand_int_frequency_weighted(int state[], int min_index, int max_index) {

	double total, rnd, cumsum;

	total = 0.;
	for (int i = min_index; i < max_index; i++) {
		total += english_monograms[state[i]];
	}

	rnd = frand();
	cumsum = 0.;
	for (int i = min_index; i < max_index; i++) {
		cumsum += english_monograms[state[i]]/total;
		if (cumsum > rnd) {
			return i;
		}
	}

	return max_index - 1;
}



// Load n-gram data from file. 

float* load_ngrams(char *ngram_file, int ngram_size, bool verbose) {

	FILE *fp;
	int i, n_ngrams, freq, indx;
	char ngram[MAX_NGRAM_SIZE];
	float *ngram_data, total;

	if (verbose) {
		printf("\nLoading ngrams...");
	}

	// Allocate memory for the ngram data.

	n_ngrams = int_pow(ALPHABET_SIZE, ngram_size);
	ngram_data = malloc(n_ngrams*sizeof(float));

	// Initialise. 

	for (i = 0; i < n_ngrams; i++) {
		ngram_data[i] = 0.;
	}

	// Read raw data from file. 

	fp = fopen(ngram_file, "r");

	while(!feof (fp)) {
		fscanf(fp, "%s\t%d", ngram, &freq);
		indx = ngram_index_str(ngram, ngram_size);
		ngram_data[indx] = freq;
	}

	fclose(fp);

	// Log-scale.

	total = 0.;
	for (i = 0; i < n_ngrams; i++) {
		ngram_data[i] = log(1. + ngram_data[i]);
		total += ngram_data[i];
	}

	// Normalise.

	for (i = 0; i < n_ngrams; i++) {
		ngram_data[i] /= total;
	}	

	if (verbose) {
		printf("...finished.\n\n");
	}

	return ngram_data;
}



// Returns the index of an n-gram. For example, the index of 'TH' would be 
// 19 + 7*26 = 201, as 'T' and 'H' and the 19th and 7th letters of the alphabet 
// respectively. 
 
int ngram_index_str(char *ngram, int ngram_size) {

	int c, index = 0, base = 1;

	for (int i = 0; i < ngram_size; i++) {
		c = toupper(ngram[i]) - 'A';
		index += c*base;
		base *= ALPHABET_SIZE;
	}

	return index;
}

int ngram_index_int(int *ngram, int ngram_size) {

	int index = 0, base = 1;

	for (int i = 0; i < ngram_size; i++) {
		index += ngram[i]*base;
		base *= ALPHABET_SIZE;
	}

	return index;
}



// Load dictionary. 

void load_dictionary(char *filename, char ***dict, int *n_dict_words, int *max_dict_word_len, bool verbose) {

	FILE *fp;
	int i, n_words, max_word_len;
	char word[MAX_DICT_WORD_LEN];

	if (verbose) {
		printf("\nLoading dictionary...\n\n");
	}

	// Count the number of words in the file. 

	fp = fopen(filename, "r");

	n_words = 0;
	max_word_len = 0;
	while(!feof (fp)) {
		fscanf(fp, "%s\n", word);
		n_words++;
		if (strlen(word) > max_word_len) {
			max_word_len = strlen(word);
		}
	}

	*max_dict_word_len = max_word_len;
	*n_dict_words = n_words;

	fclose(fp);

	if (verbose) {
		printf("%d words in dictionary, ", n_words);
		printf("longest word has %d chars.\n", max_word_len);
	}

	// Allocate memory for array of words. 

	*dict = malloc(n_words*sizeof(char*));
	for (i = 0; i < n_words; i++) {
		(*dict)[i] = malloc((max_word_len + 1)*sizeof(char));
	}

	// Fill array with words from dictionary. 

	fp = fopen(filename, "r");

	i = 0; 
	while(!feof (fp)) {
		fscanf(fp, "%s\n", word);
		strcpy((*dict)[i], word);
		i++; 
	}

	fclose(fp);

	if (verbose) {
		printf("\n...finished.\n");
	}
}



// Deallocate dictionary. 

void free_dictionary(char **dict, int n_dict_words) {

	for (int i = 0; i < n_dict_words; i++) {
		free(dict[i]);
	}
	free(dict);
}



// Find dictionary words in plaintext. 

int find_dictionary_words(char *plaintext, char **dict, int n_dict_words, int max_dict_word_len) {

	int n_matches = 0, plaintext_len, min_word_len;
	char fragment[MAX_DICT_WORD_LEN], *dict_word;

	// printf("\nn_dict_words, max_dict_word_len = %d, %d\n", n_dict_words, max_dict_word_len);

	plaintext_len = strlen(plaintext);
	min_word_len = 3;

	for (int i = 0; i < plaintext_len - min_word_len; i++) {
		for (int word_len = min_word_len; word_len < min(max_dict_word_len, plaintext_len - i); word_len++) {

			//  fragment = plaintext[i : i + word_len]

			for (int j = 0; j < word_len; j++) {
				fragment[j] = plaintext[i + j];
			}
			fragment[word_len] = '\0'; 

			// Check if fragment is in dictionary. 

			for (int k = 0; k < n_dict_words; k++) {
				dict_word = dict[k];
				if (strlen(dict_word) > word_len) {
					continue ;
				} else if (strlen(dict_word) < word_len) {
					break ;
				} else if (strcmp(dict_word, fragment) == 0 ) {
					printf("%s\n", fragment);
					n_matches++;
					break ;
				}
			}

		}
	}

	return n_matches;
}


// Estimate the cycleword length from the ciphertext. 

void estimate_cycleword_lengths(
	int text[], 
	int len, 
	int max_cycleword_len, 
	double n_sigma_threshold,
	double ioc_threshold,
	int *n_cycleword_lengths, 
	int cycleword_lengths[], 
	bool verbose) {

	int i, j, caesar_column[MAX_CIPHER_LENGTH]; 
	double mu, std, max_ioc, current_ioc, 
		mu_ioc[MAX_CYCLEWORD_LEN], mu_ioc_normalised[MAX_CYCLEWORD_LEN], word_len_norm_ioc[MAX_CYCLEWORD_LEN];
	bool threshold;

	// Compute the mean IOC for each candidate cycleword length. 

	for (i = 1; i <= max_cycleword_len; i++) {
		mu_ioc[i - 1] = mean_ioc(text, len, i, caesar_column);
	}

	// Normalise (Z-score). 

	mu = vec_mean(mu_ioc, max_cycleword_len);
	std = vec_stddev(mu_ioc, max_cycleword_len);

	if (verbose) {
		printf("\ncycleword mu,std = %.3f, %.6f\n", mu, std);
	}

	for (i = 0; i < max_cycleword_len; i++) {
		mu_ioc_normalised[i] = (mu_ioc[i] - mu)/std;
	}

	// Select only those above n_sigma_threshold and sort by mean IOC. 

	// TODO: the sorting by max IOC makes this code ugly - rewrite! 

	*n_cycleword_lengths = 0;
	current_ioc = 1.e6;
	for (i = 0; i < max_cycleword_len; i++) {
		threshold = false;
		max_ioc = 0.;
		for (j = 0; j < max_cycleword_len; j++) {
			if (mu_ioc_normalised[j] > n_sigma_threshold && mu_ioc[j] > ioc_threshold && mu_ioc_normalised[j] > max_ioc && mu_ioc_normalised[j] < current_ioc) {
				threshold = true;
				max_ioc = mu_ioc_normalised[j];
				cycleword_lengths[i] = j + 1;
			}
		}
		current_ioc = max_ioc;
		if (threshold) {
			(*n_cycleword_lengths)++;
		}
	}

	// Compute English word length normalised score. 

	for (i = 0; i < max_cycleword_len; i++) {
		if (i < n_english_word_length_frequency_letters) {
		word_len_norm_ioc[i] = english_word_length_frequencies[i]*mu_ioc[i];
		} else {
			word_len_norm_ioc[i] = 0.;
		}
	}

	// Normalise (Z-score). 

	mu = vec_mean(word_len_norm_ioc, max_cycleword_len);
	std = vec_stddev(word_len_norm_ioc, max_cycleword_len);

	for (i = 0; i < max_cycleword_len; i++) {
		word_len_norm_ioc[i] = (word_len_norm_ioc[i] - mu)/std;
	}

	if (verbose) {
		printf("\nlen\tmean IOC\n");
		for (i = 0; i < max_cycleword_len; i++) {
			if (verbose) {
				printf("%d\t%.4f\n", i + 1, mu_ioc[i]);
			}
		}
	}

	if (verbose) {
		printf("\ncycleword_lengths =\t");
		for (i = 0; i < *n_cycleword_lengths; i++) {
			printf("%d\t", cycleword_lengths[i]);
		}
		printf("\n\n");
	}

	return ;
}



// Given the cycleword length, compute the mean IOC. 

double mean_ioc(int text[], int len, int len_cycleword, int *caesar_column) {

	int i, k;
	double weighted_ioc = 0.;

	for (k = 0; k < len_cycleword; k++) {

		i = 0;
		while (len_cycleword*i + k < len) {
			caesar_column[i] = text[len_cycleword*i + k];
			i++;
		}

		weighted_ioc += index_of_coincidence(caesar_column, i);
	}

	return weighted_ioc/len_cycleword;
}



// Mean and standard deviation of a 1D array. 

double vec_mean(double vec[], int len) {
	int i;
	double total = 0.;

	for (i = 0; i < len; i++) {
		total += vec[i];
	}
	return total/len;
}



double vec_stddev(double vec[], int len) {

	int i;
	double mu, sumdev = 0.;

	mu = vec_mean(vec, len);

	for (i = 0; i < len; i++) {
		sumdev += pow(vec[i] - mu, 2); 
	}

    return sqrt(sumdev/len);
}


void vec_print(int vec[], int len) {
	for (int i = 0; i < len; i++) {
		printf("%d ", vec[i]);
	}
	printf("\n");
}


// Print plaintext from indices. 

void print_text(int indices[], int len) {

	for (int i = 0; i < len; i++) {
		printf("%c", indices[i] + 'A');
	}
	return ;
}



// Compute the index of each char. A -> 0, B -> 1, ..., Z -> 25

void ord(char *text, int indices[]) {

	for (int i = 0; i < strlen(text); i++) {
		indices[i] = toupper(text[i]) - 'A';
	}

	return ;
}



// Count the frequencies of char in plaintext. 

void tally(int plaintext[], int len, int frequencies[], int n_frequencies) {

	int i;

	// Initialise frequencies to zero. 
	for (i = 0; i < n_frequencies; i++) {
		frequencies[i] = 0;
	}

	// Tally. 
	for (i = 0; i < len; i++) {
		frequencies[plaintext[i]]++;
	}

	return ;
}



// Friedman's Index of Coincidence. 

float index_of_coincidence(int plaintext[], int len) {

	int i, frequencies[ALPHABET_SIZE];
	double ioc = 0.;

	// Compute plaintext char frequencies. 
	tally(plaintext, len, frequencies, ALPHABET_SIZE);

	for (i = 0; i < ALPHABET_SIZE; i++) {
        ioc += frequencies[i]*(frequencies[i] - 1);
    }

    ioc /= len*(len - 1);
    return ioc;
}



// Straight alphabet - ABCDEFGHIJKLMNOPQRSTUVWXYZ

void straight_alphabet(int keyword[], int len) {
	for (int i = 0; i < len; i++) {
		keyword[i] = i;
	}

	return ;
}



bool file_exists(const char *filename) {
	FILE *file;
	file = fopen(filename, "r");
    if (file) {
        fclose(file);
        return true;
    }
    return false;
}



// Shuffle array -- ref: https://stackoverflow.com/questions/6127503/shuffle-array-in-c

void shuffle(int *array, size_t n) 
{
    if (n > 1) 
    {
        size_t i;
        for (i = 0; i < n - 1; i++) 
        {
          size_t j = i + rand() / (RAND_MAX / (n - i) + 1);
          int t = array[j];
          array[j] = array[i];
          array[i] = t;
        }
    }
}



void vec_copy(int src[], int dest[], int len) {
	for (int i = 0; i < len; i++) dest[i] = src[i]; 
}



int int_pow(int base, int exp)
{
    int result = 1;
    while (exp)
    {
        if (exp % 2)
           result *= base;
        exp /= 2;
        base *= base;
    }
    return result;
}



// Returns a random int in [min, max). 

int rand_int(int min, int max) {
   return min + rand() % (max - min); // result in [min, max)
}


double frand() {
  return ((double) rand())/((double) RAND_MAX); // result in [0, 1]
}

