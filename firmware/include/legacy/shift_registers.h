#ifndef __SHIFT_REGISTERS_H__
#define __SHIFT_REGISTERS_H__

#ifdef __cplusplus
extern "C"
{
#endif

// void reset_cd22m_sr(void);
// void reset_bruz_sr(void);
// void set_bruz_sr_data(int data);
// void set_cd22m_sr_data(int data);
// void clock_bruz_sr(void);
// void clock_cd22m_sr(void);

typedef enum {
  BRUZ_SR,
  CD22M_SR
} sr_t;

void sr_reset(sr_t sr);
void sr_set(sr_t sr, int data);
void sr_clock(sr_t sr);
void sr_start(sr_t sr);
void sr_shift_en(sr_t sr);

#ifdef __cplusplus
}
#endif

#endif /* __SHIFT_REGISTERS_H__ */
