#ifndef RECORD_H
#define RECORD_H

#endif // RECORD_H

class Record{
private:float high, low, avg;
public:Record(){}
    Record(float high, float low, float avg){
        this->high = high;
        this->low = low;
        this->avg = avg;
    }
    float getHigh(){
        return high;
    }
    float getLow(){
        return low;
    }
    float getAvg(){
        return avg;
    }
};
