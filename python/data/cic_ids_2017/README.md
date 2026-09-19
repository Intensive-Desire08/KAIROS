# CIC-IDS2017 Dataset

Per Technical Specification Section 4.3, the primary training dataset is
**CIC-IDS2017 (benign traffic only)**.

## Getting the dataset

1. Download from the Canadian Institute for Cybersecurity:
   https://www.unb.ca/cic/datasets/ids-2017.html
2. Extract the CSVs and concatenate the days you want to use.
3. Run preprocessing to filter to benign-only traffic and map columns onto
   the KAIROS feature schema (`ml_service.constants.FEATURE_ORDER`):
   - `data_length`, `protocol_encoded`, `packet_rate`, `avg_packet_size`,
     `destination_diversity`, `failed_logins`, `session_duration`,
     `total_packets`
4. Save the result as `benign_only.csv` in this directory.
5. Train: `python training/train.py --data ./data/cic_ids_2017/benign_only.csv`

## Development without the real dataset

`training/data_loader.py :: generate_synthetic_benign()` generates realistic
synthetic benign traffic matching the feature schema, so the full
train → validate → deploy pipeline is runnable without downloading anything.
This is what `python training/train.py` uses by default (no `--data` flag).
Swap in the real CIC-IDS2017 data before production deployment — synthetic
data is for pipeline development and CI only.
