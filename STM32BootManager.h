/*
 * STM32BootManager.h
 *
 *  Created on: Jun 18, 2023
 *      Author: Marcin Dzieciątkowski
 */

#ifndef STM32BOOTMANAGER_H_
#define STM32BOOTMANAGER_H_

#include <cstdint>
#include <functional>

#define START_ADDRESS 0x08006000
#define END_ADDRESS 0x08020000
#define PAGE_SIZE 0x800
#define METADATA_SIZE 4 // dont calculate last 4 bytes for crc

class STM32BootManager {
public:
	struct BootOperations{
		void (*unlock)();
		void (*lock)();
		bool (*write)(uint32_t, uint8_t*, size_t);
		bool (*erase_app)();
		void (*deinit_peripherals)();
		void (*deinit_systick)();
	};


	STM32BootManager(BootOperations* operations = nullptr) :
	m_operations(operations),
	m_was_erased(false),
	m_write_pointer(START_ADDRESS)
	{

	}
	virtual ~STM32BootManager() {}

	bool read(const uint32_t& address, uint8_t* dst, const size_t& size)
	{
		if(m_operations == nullptr) return false;
		m_operations->unlock();
		const uint8_t* src = (const unsigned char*)address;
		for(uint16_t i = 0;i<size / 4;++i){
			*(((uint32_t*)dst) + i) = *(((uint32_t*)src) + i);
		}
		m_operations->lock();
		return true;
	}
	bool write(const uint32_t& start, uint8_t* data, const size_t& size)
	{
		if(m_operations == nullptr) return false;
		if(!m_was_erased) if(!erase_app()) return false;
		m_operations->unlock();
		bool ret = m_operations->write(start, data, size);
		m_operations->lock();
		return ret;
	}
	bool write_continously(uint8_t* buffer, const size_t& size)
	{
		if(!m_was_erased) if(!erase_app()) return false;
		bool ret = write(m_write_pointer, buffer, size);
		if(ret) m_write_pointer += size;
		return ret;
	}
	void jump_to_app()
	{
		if(m_operations == nullptr) return;
		m_operations->deinit_peripherals();
		void __attribute__((noreturn)) (*SysJump)(void) = (void (*)(void)) (*((uint32_t *)(START_ADDRESS + 4)));
		m_operations->deinit_systick();
		SysJump();
	}
	uint32_t calculate_crc() const
	{
		if(m_operations == nullptr) return 0;
		m_operations->unlock();
		uint32_t crc = 0;
		uint32_t len = (END_ADDRESS - START_ADDRESS) - METADATA_SIZE;
		const uint32_t CRC32_POLYNOMIALL = 0xEDB88320;
		static uint32_t table[256];
		static bool have_table = false;

		if (!have_table) {
			for (int i = 0; i < 256; i++) {
				uint32_t rem = i;
				for (int j = 0; j < 8; j++) {
					if (rem & 1) {
						rem >>= 1;
						rem ^= CRC32_POLYNOMIALL;
					} else
						rem >>= 1;
				}
				table[i] = rem;
			}
			have_table = true;
		}

		crc = ~crc;
		const unsigned char* dp = (const unsigned char*)START_ADDRESS;
		while(len > 0){
			crc = table[(crc ^ *dp) & 0xFF] ^ (crc >> 8);
			++dp;
			--len;
		}
		m_operations->lock();
		return ~crc;
	}

	void set_bootloader_operations(BootOperations* operations) { m_operations = operations; }
	uint32_t get_app_size() const { return END_ADDRESS - START_ADDRESS; }
	uint32_t get_app_start() const { return START_ADDRESS; }
	uint32_t get_app_end() const { return END_ADDRESS; }
	uint16_t get_page_size() const { return PAGE_SIZE; }
protected:
	bool erase_app()
	{
		if(m_operations == nullptr) return false;
		m_operations->unlock();
		bool ret = m_operations->erase_app();
		m_operations->lock();
		if(ret) m_was_erased = true;
		return ret;
	}
private:
	BootOperations* m_operations;
	bool m_was_erased;
	uint32_t m_write_pointer;
};

#ifdef STM32_G0
STM32BootManager::BootOperations* G0_Operations()
{
	static STM32BootManager::BootOperations op;
	op.unlock = [](){
		HAL_FLASH_Unlock();
	};

    op.lock = [](){
    	HAL_FLASH_Lock();
    };

    op.erase_app = [](){
    	FLASH_EraseInitTypeDef erase;
		erase.TypeErase = FLASH_TYPEERASE_PAGES;
		erase.Page = (END_ADDRESS - START_ADDRESS) / PAGE_SIZE;
		erase.NbPages = FLASH_PAGE_NB - erase.Page;
		erase.Banks = FLASH_BANK_1;
		uint32_t t;
		auto status = HAL_FLASHEx_Erase(&erase, &t);
		return status == HAL_OK;
    };

    op.deinit_peripherals = [](){
    	// some deinits
	    HAL_DeInit();
    };

    op.deinit_systick = [](){
    	SysTick->CTRL = 0;
		SysTick->LOAD = 0;
		SysTick->VAL = 0;
		SCB->VTOR = START_ADDRESS;
		__set_MSP(*(__IO uint32_t*)START_ADDRESS);
    };

	 op.write = [](uint32_t address, uint8_t* data, size_t size){
		 for (uint32_t i = 0; i < size;)
		 {
			if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, *((uint64_t*)data)) == HAL_OK){
				if (*(uint8_t*)address != *data){
					return false;
				}
				address += 8;
				data += 8;
				i += 8;
			 } else return false;
		  }
		  return true;
	  };
	 return &op;
}
#endif

#endif /* STM32BOOTMANAGER_H_ */
