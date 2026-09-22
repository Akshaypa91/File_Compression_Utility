struct HuffmanNodeCompare {
    bool operator()(const HuffmanNode* a,
                    const HuffmanNode* b) const {
        return a->frequency > b->frequency;
    }
};